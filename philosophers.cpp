#include <iostream>
#include <stdlib.h>
#include <getopt.h>
#include <fstream>
#include <vector>
#include <pthread.h>
#include <mutex>
#include <unistd.h>

/*
 * To generate arbitrary interleavings, a thinking or eating (drinking) philosopher should call the linux usleep function with a randomly chosen argument.
 * I suggest values in the range of 1–1,000 microseconds (1,000–1,000,000 nanoseconds).
 * If you make the sleeps too short, you’ll serialize on the output lock, and execution will get much less interesting.
 * For simplicity and for ease of grading, each drinking session should employ all adjacent bottles (not the arbitrary subset allowed by Chandy and Misra).
 */

/*
 * To avoid interleaving of output messages, you’ll need to use a lock to protect your access to stdout.
 * During testing, you may find it helpful to redirect stdout to a file.
 */

/*
 * To generate pseudo-random numbers, I recommend the random_r library routine.
 * To better test your program, you may want to arrange for it to use a different “seed”
 * for the random number generator on every run, either by passing something like the time of day
 * to srandom_r or by taking a seed as an optional command-line parameter.
 */

enum class DiningState { THINKING = 1, HUNGRY, EATING };
enum class DrinkingState { TRANQUIL = 1, THIRSTY, DRINKING };

class Resource {
public:
    class Fork {
    public:
        bool reqf = false;
        bool dirty = true;
        bool hold = false;
    } fork;

    class Bottle {
    public:
        bool bot = false;
        bool reqb = false;
        bool need = false;
    } bottle;

    friend std::ostream& operator<<(std::ostream &out, Resource res) {
        if (res.fork.hold == res.fork.reqf) return out << "error";
        return out << (res.fork.dirty ? "dirty" : "clean") << " " << (res.fork.hold ? "fork" : "reqf");
    }
};

int parse_opts(int argc, char **argv);
std::vector<std::vector<std::pair<int, Resource>>> init_graph(int mode);
void *philosopher(void *pid);

int p_cnt;
int session_cnt = 20;
std::string conf_path;
std::atomic_bool start;
std::vector<DiningState> dining_states;
std::vector<DrinkingState> drinking_states;
std::vector<std::vector<std::pair<int, Resource>>> graph;
std::vector<std::vector<Resource>> resources;
std::vector<unsigned int> rand_seeds;
std::mutex g_lock;

int main(int argc, char **argv) {
    graph = init_graph(parse_opts(argc, argv));
    std::cout << "press any key to continue." << std::endl;
    getchar();

    // TODO
    std::cout<<"graph initialization:"<<std::endl;
    for (int i = 1; i < graph.size(); i++) {
        std::cout << i << ": ";
        for (const auto &adjacent : graph[i]) {
            std::cout << adjacent.first << " (" << adjacent.second << ") ";
        }
        std::cout << std::endl;
    }
    printf("config: %d philosophers will eat %d times.\n\n", p_cnt, session_cnt);
    dining_states.resize(static_cast<unsigned long>(p_cnt), DiningState::THINKING);
    drinking_states.resize(static_cast<unsigned long>(p_cnt), DrinkingState::TRANQUIL);

//    resources.resize(static_cast<unsigned long>(p_cnt) + 1);
//    r1a.fork.hold = r2a.fork.hold = r3a.fork.hold = r4a.fork.hold = r5a.fork.hold = true;
//    r1b.fork.reqf = r2b.fork.reqf = r3b.fork.reqf = r4b.fork.reqf = r5b.fork.reqf = true;
//    resources[1].push_back(r1a);
//    resources[2].push_back(r1b);
//    resources[2].push_back(r2a);
//    resources[3].push_back(r2b);
//    resources[3].push_back(r3a);
//    resources[4].push_back(r3b);
//    resources[4].push_back(r4a);
//    resources[5].push_back(r4b);
//    resources[5].push_back(r5b);
//    resources[1].push_back(r5a);

    pthread_t threads[p_cnt];
    rand_seeds.resize(static_cast<unsigned long>(p_cnt));
    for (long i = 0; i < p_cnt; i++) {
        pthread_create(&threads[i], nullptr, philosopher, (void *) i);
        rand_seeds[i] = static_cast<unsigned int>(i * 2);
    }
    start = true;
    for (int i = 0; i < p_cnt; i++) {
        pthread_join(threads[i], nullptr);
    }
    return 0;
}

int parse_opts(int argc, char **argv) {
    int opt;
    static struct option opts[] = {
            {"session", required_argument, nullptr, 's'},
            {"filename", required_argument, nullptr, 'f'},
            {nullptr, no_argument, nullptr, 0},
    };
    while ((opt = getopt_long(argc, argv, ":s:f:-", opts, nullptr)) != EOF) {
        switch (opt) {
            case 's': session_cnt = static_cast<int>(std::strtol(optarg, nullptr, 10)); break;
            case 'f': conf_path = optarg; break;
            case ':': std::cerr << "invalid option: needs a value" << opt << std::endl; break;
            case '?': std::cout << "usage: philosophers -s <session_count> -f <filename> [-]" << std::endl; exit(-1);
            default: break;
        }
    }
    std::cout << "sessions count:         " << (session_cnt = session_cnt < 1 ? 20 : session_cnt) << std::endl;
    for(; optind < argc; optind++){
        if (!strcmp(argv[optind], "-")) {
            return 2;
        }
    }
    if (conf_path.length()) {
        std::cout << "configuration path:     " << conf_path << std::endl;
        return 1;
    }
    return 0;
}

std::vector<std::vector<std::pair<int, Resource>>> init_graph(int mode) {
    if (mode == 1) {
        std::ifstream file(conf_path);
        if (file.good()) {
            int p1, p2, n = 0;
            file >> p_cnt;
            std::vector<std::vector<std::pair<int, Resource>>> graph(static_cast<unsigned long>(p_cnt + 1));
            for (int i = 0; i < p_cnt; i++) {
                file >> p1 >> p2;
                if (p1 < 1 || p2 < 1 || p1 > p_cnt || p2 > p_cnt) {
                    std::cerr << "error: invalid graph" << std::endl;
                    exit(-1);
                }
                Resource pos = Resource(), neg = Resource();
                pos.fork.hold = true;
                neg.fork.reqf = true;
                graph[p1].push_back(std::make_pair(p2, p1 < p2 ? pos : neg));
                graph[p2].push_back(std::make_pair(p1, p1 < p2 ? neg : pos));
                n++;
            }
            if (n < p_cnt - 1 || n > (p_cnt * (p_cnt - 1) / 2)) {
                std::cerr << "error: invalid graph" << std::endl;
                exit(-1);
            }
            return graph;
        } else {
            std::cerr << "error: file '" << conf_path << "' not found" << std::endl;
            exit(-1);
        }

    } else if (mode == 2) {
        int p1, p2, n = 0;
        std::cout << "number of philosophers: ";
        std::cin >> p_cnt;
        std::cout << "edge pairs (0 to exit):" << std::endl;
        std::vector<std::vector<std::pair<int, Resource>>> graph(static_cast<unsigned long>(p_cnt + 1));
        while (true) {
            std::cin >> p1 >> p2;
            if (p1 < 1 || p2 < 1 || p1 > p_cnt || p2 > p_cnt) break;
            Resource pos = Resource(), neg = Resource();
            pos.fork.hold = true;
            neg.fork.reqf = true;
            graph[p1].push_back(std::make_pair(p2, p1 < p2 ? pos : neg));
            graph[p2].push_back(std::make_pair(p1, p1 < p2 ? neg : pos));
            n++;
        }
        if (n < p_cnt - 1 || n > (p_cnt * (p_cnt - 1) / 2)) {
            std::cerr << "error: invalid graph" << std::endl;
            exit(-1);
        }
        return graph;

    }

    p_cnt = 5;
    Resource r1a = Resource(), r2a = Resource(), r3a = Resource(), r4a = Resource(), r5a = Resource();
    Resource r1b= Resource(), r2b = Resource(), r3b = Resource(), r4b = Resource(), r5b = Resource();
    r1a.fork.hold = r2a.fork.hold = r3a.fork.hold = r4a.fork.hold = r5a.fork.hold = true;
    r1b.fork.reqf = r2b.fork.reqf = r3b.fork.reqf = r4b.fork.reqf = r5b.fork.reqf = true;

    // todo: convert to acyclic graph
    return {{},
            {std::make_pair(2, r1a), std::make_pair(5, r5a)},
            {std::make_pair(3, r2a), std::make_pair(1, r1b)},
            {std::make_pair(4, r3a), std::make_pair(2, r2b)},
            {std::make_pair(5, r4a), std::make_pair(3, r3b)},
            {std::make_pair(1, r5b), std::make_pair(4, r4b)}};
}

void *philosopher(void *pid) {
    while (!start.load());
    long id = (long) pid;
    auto micro_sec = rand_r(&rand_seeds[id]) % 1000;
    usleep(static_cast<useconds_t>(micro_sec)); // 0 to 1000 microseconds

    std::lock_guard <std::mutex> lock(g_lock);
    std::cout << "philosopher " << id << " is ";
    switch (dining_states[id]) {
        case DiningState::THINKING:
            std::cout << "thinking" << std::endl;
            break;
        case DiningState::HUNGRY:
            std::cout << "hungry" << std::endl;
            break;
        case DiningState::EATING:
            std::cout << "eating" << std::endl;
            break;
    }
    return nullptr;
}