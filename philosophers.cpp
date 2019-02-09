#include <iostream>
#include <getopt.h>
#include <fstream>
#include <vector>

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
int parse_opts(int argc, char **argv);
std::vector<std::vector<int>> init_graph(int mode);
bool validate_graph(int p_cnt, std::vector<std::vector<int>> graph);

int session_cnt = 20;
std::string conf_path;

int main(int argc, char **argv) {
    std::vector<std::vector<int>> graph = init_graph(parse_opts(argc, argv));
    std::cout << "press any key to continue." << std::endl;
    getchar();

    for (int i = 1; i < graph.size(); i++) {
        std::cout << i << ": ";
        for (const auto &adjacent : graph[i]) {
            std::cout << adjacent << " ";
        }
        std::cout << std::endl;
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
    while ((opt = getopt_long(argc, argv, "s:f:-", opts, nullptr)) != EOF) {
        switch (opt) {
            case 's': session_cnt = static_cast<int>(std::strtol(optarg, nullptr, 10)); break;
            case 'f': conf_path = optarg; break;
            case ':': std::cerr << "invalid option: needs a value" << opt << std::endl; break;
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

std::vector<std::vector<int>> init_graph(int mode) {
    if (mode == 1) {
        std::ifstream file(conf_path);
        if (file.good()) {
            int p_cnt, p1, p2;
            file >> p_cnt;
            std::vector<std::vector<int>> graph(static_cast<unsigned long>(p_cnt + 1));
            for (int i = 0; i < p_cnt; i++) {
                file >> p1 >> p2;
                if (p1 < 1 || p2 < 1 || p1 > p_cnt || p2 > p_cnt) {
                    std::cerr << "error: invalid graph" << std::endl;
                    exit(-1);
                }
                graph[p1].push_back(p2);
                graph[p2].push_back(p1);
            }
            auto n = graph.size();
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
        int p_cnt, p1, p2;
        std::cout << "number of philosophers: ";
        std::cin >> p_cnt;
        std::cout << "edge pairs (0 to exit):" << std::endl;
        std::vector<std::vector<int>> graph(static_cast<unsigned long>(p_cnt + 1));
        while (true) {
            std::cin >> p1 >> p2;
            if (p1 < 1 || p2 < 1 || p1 > p_cnt || p2 > p_cnt) break;
            graph[p1].push_back(p2);
            graph[p2].push_back(p1);
        }
        auto n = graph.size();
        if (n < p_cnt - 1 || n > (p_cnt * (p_cnt - 1) / 2)) {
            std::cerr << "error: invalid graph" << std::endl;
            exit(-1);
        }
        return graph;

    }

    return {{}, {2, 5}, {3, 1}, {4, 2}, {5, 3}, {1, 4}};
}