#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <pthread.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <mutex>

/**
 * Drinking Philosophers Problem - Chandy and Misra's solution
 *
 * @cite    https://www.cs.utexas.edu/users/misra/scannedPdf.dir/DrinkingPhil.pdf
 * @author  Jiupeng Zhang (jzh149@ur.rochester.edu)
 */
enum class DiningState {
    THINKING = 1, HUNGRY, EATING
};

enum class DrinkingState {
    TRANQUIL = 1, THIRSTY, DRINKING
};

class Resource {
public:
    class Fork {
    public:
        pthread_mutex_t lock{};
        pthread_cond_t condition{};
        volatile bool hold{};
        volatile bool reqf{};
        volatile bool dirty = true;
    } fork;

    class Bottle {
    public:
        pthread_mutex_t lock{};
        pthread_cond_t condition{};
        volatile bool hold{};
        volatile bool reqb{};
        // volatile bool need{}; // ignore for simplicity
    } bottle;

    friend std::ostream &operator<<(std::ostream &out, Resource res) {
        if (res.fork.hold == res.fork.reqf) return out << "error";
        return out << (res.bottle.hold ? "bottle" : "reqb  ") << " " << (res.fork.hold ? "fork" : "reqf");
    }
};

int parse_opts(int argc, char **argv);

std::vector<std::vector<std::pair<int, Resource>>> init_graph(int mode);

void *philosopher(void *pid);

void tranquil(long id);

void drinking(long id);

void report(long id);

void report_dining(long id);

void report_drinking(long id);

void send_reqf(long from, long to);

void send_fork(long from, long to);

void send_reqb(long from, long to);

void send_bottle(long from, long to);

/*
 * To generate arbitrary interleavings, a thinking or eating (drinking) philosopher should call the linux usleep
 * function with a randomly chosen argument. I suggest values in the range of 1–1,000 microseconds. If you make the
 * sleeps too short, you’ll serialize on the output lock, and execution will get much less interesting.
 */
constexpr long TRANQUIL_MIN = 1, TRANQUIL_MAX = 1000; // in ms
constexpr long DRINKING_MIN = 1, DRINKING_MAX = 1000; // in ms
constexpr long TRANQUIL_RANGE = TRANQUIL_MAX - TRANQUIL_MIN;
constexpr long DRINKING_RANGE = DRINKING_MAX - DRINKING_MIN;

bool debug = false;
int p_cnt;
int session_cnt = 20;
std::string conf_path;
std::atomic_bool start;
std::vector<DiningState> dining_states;
std::vector<DrinkingState> drinking_states;
std::vector<std::vector<std::pair<int, Resource>>> graph;
std::vector<unsigned int> rand_seeds;
std::mutex print_lock;

int main(int argc, char **argv) {
    graph = init_graph(parse_opts(argc, argv));
    std::cout << "press any key to continue." << std::endl;
    getchar();

    std::cout << "graph initialization:" << std::endl;
    for (int i = 0; i < graph.size(); i++) {
        std::cout << i << ": ";
        for (const auto &adjacent : graph[i]) {
            std::cout << adjacent.first << " (" << adjacent.second << ") ";
        }
        std::cout << std::endl;
    }
    printf("config: %d philosophers will drink %d times.\n\n", p_cnt, session_cnt);
    dining_states.resize(static_cast<unsigned long>(p_cnt), DiningState::THINKING);
    drinking_states.resize(static_cast<unsigned long>(p_cnt), DrinkingState::TRANQUIL);

    pthread_t threads[p_cnt];
    rand_seeds.resize(static_cast<unsigned long>(p_cnt));
    srand(static_cast<unsigned int>(time(nullptr)));
    for (long i = 0; i < p_cnt; i++) {
        pthread_create(&threads[i], nullptr, philosopher, (void *) i);
        rand_seeds[i] = static_cast<unsigned int>(rand());
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
            {"session",  required_argument, nullptr, 's'},
            {"filename", required_argument, nullptr, 'f'},
            {nullptr,    no_argument,       nullptr, 0},
    };
    while ((opt = getopt_long(argc, argv, ":s:f:-d", opts, nullptr)) != EOF) {
        switch (opt) {
            case 's':
                session_cnt = static_cast<int>(std::strtol(optarg, nullptr, 10));
                break;
            case 'f':
                conf_path = optarg;
                break;
            case 'd':
                debug = true;
                break;
            case ':':
                std::cerr << "invalid option: needs a value" << opt << std::endl;
                break;
            case '?':
                std::cout << "usage: philosophers -s <session_count> -f <filename> [-]" << std::endl;
                exit(-1);
            default:
                break;
        }
    }
    std::cout << "sessions count:         " << (session_cnt = session_cnt < 1 ? 20 : session_cnt) << std::endl;
    for (; optind < argc; optind++) {
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
            std::vector<std::vector<std::pair<int, Resource>>> graph(static_cast<unsigned long>(p_cnt));
            for (int i = 0; i < p_cnt; i++) {
                file >> p1 >> p2;
                if (p1 < 1 || p2 < 1 || p1 > p_cnt || p2 > p_cnt) {
                    std::cerr << "error: invalid graph" << std::endl;
                    exit(-1);
                }
                Resource pos = Resource(), neg = Resource();
                pos.fork.hold = pos.bottle.hold = true;
                neg.fork.reqf = neg.bottle.reqb = true;
                graph[p1 - 1].push_back(std::make_pair(p2 - 1, p1 < p2 ? pos : neg));
                graph[p2 - 1].push_back(std::make_pair(p1 - 1, p1 < p2 ? neg : pos));
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
        std::vector<std::vector<std::pair<int, Resource>>> graph(static_cast<unsigned long>(p_cnt));
        while (true) {
            std::cin >> p1 >> p2;
            if (p1 < 1 || p2 < 1 || p1 > p_cnt || p2 > p_cnt) break;
            Resource pos = Resource(), neg = Resource();
            pos.fork.hold = pos.bottle.hold = true;
            neg.fork.reqf = neg.bottle.reqb = true;
            graph[p1 - 1].push_back(std::make_pair(p2 - 1, p1 < p2 ? pos : neg));
            graph[p2 - 1].push_back(std::make_pair(p1 - 1, p1 < p2 ? neg : pos));
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
    Resource r1b = Resource(), r2b = Resource(), r3b = Resource(), r4b = Resource(), r5b = Resource();
    r1a.fork.hold = r2a.fork.hold = r3a.fork.hold = r4a.fork.hold = r5a.fork.hold = true;
    r1b.fork.reqf = r2b.fork.reqf = r3b.fork.reqf = r4b.fork.reqf = r5b.fork.reqf = true;
    r1a.bottle.hold = r2a.bottle.hold = r3a.bottle.hold = r4a.bottle.hold = r5a.bottle.hold = true;
    r1b.bottle.reqb = r2b.bottle.reqb = r3b.bottle.reqb = r4b.bottle.reqb = r5b.bottle.reqb = true;
    return {{std::make_pair(1, r1a), std::make_pair(4, r5a)},
            {std::make_pair(2, r2a), std::make_pair(0, r1b)},
            {std::make_pair(3, r3a), std::make_pair(1, r2b)},
            {std::make_pair(4, r4a), std::make_pair(2, r3b)},
            {std::make_pair(0, r5b), std::make_pair(3, r4b)}};
}

void *philosopher(void *pid) {
    while (!start.load());
    long id = (long) pid;
    for (int session = 0; session < session_cnt;) {
        report(id);
        std::vector<std::pair<int, Resource>> refs = graph[id];

        switch (drinking_states[id]) {
            case DrinkingState::TRANQUIL:
                for (std::pair<int, Resource> ref_pair : refs) {
                    Resource resource = ref_pair.second;
                    pthread_mutex_lock(&resource.bottle.lock);
                    if (resource.bottle.hold && resource.bottle.reqb && !resource.fork.hold) {
                        send_bottle(id, ref_pair.first);
                        resource.bottle.hold = false;
                    }
                    pthread_mutex_unlock(&resource.bottle.lock);
                }
                tranquil(id);
                drinking_states[id] = DrinkingState::THIRSTY;
                break;

            case DrinkingState::THIRSTY:
                // For simplicity and for ease of grading, each drinking session should employ
                // all adjacent bottles (not the arbitrary subset allowed by Chandy and Misra).
                for (std::pair<int, Resource> ref_pair : refs) {
                    Resource resource = ref_pair.second;
                    pthread_mutex_lock(&resource.bottle.lock);
                    if (resource.bottle.hold && resource.bottle.reqb && !resource.fork.hold) {
                        send_bottle(id, ref_pair.first);
                        resource.bottle.hold = false;
                    }
                    if (!resource.bottle.hold) {
                        while (!resource.bottle.reqb) {
                            // waiting for bottle-ticket
                            pthread_cond_wait(&resource.bottle.condition, &resource.bottle.lock);
                        }
                        // single request sent
                        send_reqb(id, ref_pair.first);
                        resource.bottle.reqb = false;
                    }
                    pthread_mutex_unlock(&resource.bottle.lock);
                }
                // all bottles received
                drinking_states[id] = DrinkingState::DRINKING;
                break;

            case DrinkingState::DRINKING:
                drinking(id);
                drinking_states[id] = DrinkingState::TRANQUIL;
                session++;
                break;
        }
        report_drinking(id);

        switch (dining_states[id]) {
            case DiningState::THINKING:
                for (std::pair<int, Resource> ref_pair : refs) {
                    Resource resource = ref_pair.second;
                    pthread_mutex_lock(&resource.fork.lock);
                    if (resource.fork.hold && resource.fork.dirty && resource.fork.reqf) {
                        resource.fork.dirty = false;
                        send_fork(id, ref_pair.first);
                        resource.fork.hold = false;
                    }
                    pthread_mutex_unlock(&resource.fork.lock);
                }

                // (D1) A thinking, thirsty philosopher becomes hungry
                if (drinking_states[id] == DrinkingState::THIRSTY) {
                    dining_states[id] = DiningState::HUNGRY;
                }
                break;

            case DiningState::HUNGRY:
                for (std::pair<int, Resource> ref_pair : refs) {
                    Resource resource = ref_pair.second;
                    pthread_mutex_lock(&resource.fork.lock);
                    // fork exists, yield precedence if it is dirty
                    if (resource.fork.hold && resource.fork.dirty && resource.fork.reqf) {
                        resource.fork.dirty = false;
                        send_fork(id, ref_pair.first);
                        resource.fork.hold = false;
                    }
                    if (!resource.fork.hold) {
                        while (!resource.fork.reqf) {
                            // waiting for fork-ticket
                            pthread_cond_wait(&resource.fork.condition, &resource.fork.lock);
                        }
                        // single request sent
                        send_reqf(id, ref_pair.first);
                        resource.fork.reqf = false;
                    }
                    pthread_mutex_unlock(&resource.fork.lock);
                }

                for (std::pair<int, Resource> ref_pair : refs) {
                    Resource resource = ref_pair.second;
                    pthread_mutex_lock(&resource.fork.lock);
                    if (!resource.fork.hold && !resource.fork.reqf) {
                        // waiting for fork
                        pthread_cond_wait(&resource.fork.condition, &resource.fork.lock);
                    }
                    pthread_mutex_unlock(&resource.fork.lock);
                }

                // all forks received
                dining_states[id] = DiningState::EATING;
                break;

            case DiningState::EATING:
                for (std::pair<int, Resource> ref_pair : refs) {
                    Resource resource = ref_pair.second;
                    pthread_mutex_lock(&resource.fork.lock);
                    resource.fork.dirty = true; // already ate
                    pthread_mutex_unlock(&resource.fork.lock);
                }
                // (D2) An eating, nonthirsty philosopher starts thinking
                if (drinking_states[id] != DrinkingState::THIRSTY) {
                    dining_states[id] = DiningState::THINKING;
                }
                break;
        }
        report_dining(id);
    }
    return nullptr;
}

// (R1) Requesting a fork f:
void send_reqf(long from, long to) {
    auto it = std::find_if(graph[to].begin(), graph[to].end(), [from](std::pair<int, Resource> ref_pair) -> bool {
        return from == ref_pair.first;
    });
    if (it == graph[to].end()) {
        std::cerr << "warn: reverse edge for <" << from << "> not found" << std::endl;
        return;
    }
    // (R3) Receiving a request token for f:
    Resource resource_reverse = (*it).second;
    pthread_mutex_lock(&resource_reverse.fork.lock);
    resource_reverse.fork.reqf = true;
    pthread_cond_signal(&resource_reverse.fork.condition);
    pthread_mutex_unlock(&resource_reverse.fork.lock);
}

// (R2) Releasing a fork f:
void send_fork(long from, long to) {
    auto it = std::find_if(graph[to].begin(), graph[to].end(), [from](std::pair<int, Resource> ref_pair) -> bool {
        return from == ref_pair.first;
    });
    if (it == graph[to].end()) {
        std::cerr << "warn: reverse edge for <" << from << "> not found" << std::endl;
        return;
    }
    // (R4) Receiving a fork f:
    Resource resource_reverse = (*it).second;
    pthread_mutex_lock(&resource_reverse.fork.lock);
    resource_reverse.fork.dirty = false;
    resource_reverse.fork.hold = true;
    pthread_cond_signal(&resource_reverse.fork.condition);
    pthread_mutex_unlock(&resource_reverse.fork.lock);
}

// (R1) Requesting a Bottle:
void send_reqb(long from, long to) {
    auto it = std::find_if(graph[to].begin(), graph[to].end(), [from](std::pair<int, Resource> ref_pair) -> bool {
        return from == ref_pair.first;
    });
    if (it == graph[to].end()) {
        std::cerr << "warn: reverse edge for <" << from << "> not found" << std::endl;
        return;
    }
    // (R3) Receive Request for a Bottle:
    Resource resource_reverse = (*it).second;
    pthread_mutex_lock(&resource_reverse.fork.lock);
    resource_reverse.bottle.reqb = true;
    pthread_cond_signal(&resource_reverse.fork.condition);
    pthread_mutex_unlock(&resource_reverse.fork.lock);
}

// (R2) Send a Bottle:
void send_bottle(long from, long to) {
    auto it = std::find_if(graph[to].begin(), graph[to].end(), [from](std::pair<int, Resource> ref_pair) -> bool {
        return from == ref_pair.first;
    });
    if (it == graph[to].end()) {
        std::cerr << "warn: reverse edge for <" << from << "> not found" << std::endl;
        return;
    }
    // (R4) Receive a Bottle:
    Resource resource_reverse = (*it).second;
    pthread_mutex_lock(&resource_reverse.bottle.lock);
    resource_reverse.bottle.hold = true;
    pthread_mutex_unlock(&resource_reverse.bottle.lock);
}

void tranquil(long id) {
    usleep(static_cast<useconds_t>(TRANQUIL_MIN + rand_r(&rand_seeds[id]) % TRANQUIL_RANGE));
}

void drinking(long id) {
    usleep(static_cast<useconds_t>(DRINKING_MIN + rand_r(&rand_seeds[id]) % DRINKING_RANGE));
}

void report_drinking(long id) {
    if (!debug && drinking_states[id] != DrinkingState::DRINKING) {
        return;
    }
    std::lock_guard<std::mutex> lock(print_lock);
    std::cout << "philosopher " << id + 1 << " ";
    switch (drinking_states[id]) {
        case DrinkingState::TRANQUIL:
            std::cout << "tranquil" << std::endl;
            break;
        case DrinkingState::THIRSTY:
            std::cout << "thirsty" << std::endl;
            break;
        case DrinkingState::DRINKING:
            std::cout << "drinking" << std::endl;
            break;
    }
}

void report_dining(long id) {
    if (!debug && dining_states[id] != DiningState::THINKING) {
        return;
    }
    std::lock_guard<std::mutex> lock(print_lock);
    std::cout << "philosopher " << id << " ";
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
}

void report(long id) {
    if (!debug) return;

    std::lock_guard<std::mutex> lock(print_lock);
    std::cout << "philosopher " << id + 1 << " is ";
    switch (drinking_states[id]) {
        case DrinkingState::TRANQUIL:
            std::cout << "tranquil (";
            break;
        case DrinkingState::THIRSTY:
            std::cout << "thirsty  (";
            break;
        case DrinkingState::DRINKING:
            std::cout << "drinking (";
            break;
    }
    switch (dining_states[id]) {
        case DiningState::THINKING:
            std::cout << "thinking)" << std::endl;
            break;
        case DiningState::HUNGRY:
            std::cout << "hungry)" << std::endl;
            break;
        case DiningState::EATING:
            std::cout << "eating)" << std::endl;
            break;
    }
}