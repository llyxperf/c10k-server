//
// Created by lz on 2/20/17.
//

#ifndef C10K_SERVER_ROUND_ROBIN_POOL_HPP
#define C10K_SERVER_ROUND_ROBIN_POOL_HPP
#include <vector>
#include <memory>
#include <atomic>
#include <spdlog/spdlog.h>
#include "utils.hpp"

namespace c10k
{
    namespace detail
    {
        // RunnerT should contain the following method:
        // - RunnerT(int max_event, const Logger &T logger)
        // - void stop()
        // - void add_new_connection(int fd)
        template<typename RunnerT>
        class RoundRobinPool {
            C10K_GEN_HAS_MEMBER(HasStop, stop);
            static_assert(HasStop<RunnerT, void()>::value, "RunnerT of RoundRobinPool should have method void stop()");
            C10K_GEN_HAS_MEMBER(HasAddNewConnection, add_new_connection);
            static_assert(HasAddNewConnection<RunnerT, void(int)>::value,
                          "RunnerT of RoundRobinPool should have method void add_new_connection(int fd)");

            using WorkerPtr = std::unique_ptr<RunnerT>;
            std::vector<std::thread> threads;
            std::vector<WorkerPtr> workers;
            std::atomic_int round_robin {0};
            const int max_event_per_loop;
            std::shared_ptr<spdlog::logger> logger;
            using LoggerT = decltype(logger);

        public:
            RoundRobinPool(int thread_num,
                             int max_event_per_loop,
                             const LoggerT &logger = spdlog::stdout_color_mt("c10k_TPool")):
                    max_event_per_loop(max_event_per_loop),
                    logger(logger)
            {
                workers.reserve(thread_num);
                threads.reserve(thread_num);
                for (int i=0; i<thread_num; ++i) {
                    logger->info("Adding worker {} into WorkerPool", i);
                    workers.emplace_back(std::make_unique<RunnerT>(max_event_per_loop, logger));
                    threads.emplace_back(std::ref(*workers[i]));
                }
            }

            int getThreadNum() const
            {
                return threads.size();
            }

            // fd must be readable and non-blocking
            void addConnection(int fd)
            {
                int cnt = round_robin.fetch_add(1);
                logger->trace("Round robin = {}, distributed to thread {}", cnt, cnt % getThreadNum());
                workers[cnt % getThreadNum()]->add_new_connection(fd);
            }

            // stopAll. Make sure you have some thread calling .join()!
            void stopAll()
            {
                logger->info("StopAll() called on WorkerThreadPool");
                std::for_each(workers.begin(), workers.end(), [](WorkerPtr &worker) {
                    worker->stop();
                });
            }

            void join()
            {
                logger->debug("Joining threads...");
                std::for_each(threads.begin(), threads.end(), [&](std::thread &t) {
                    if (t.joinable())
                        t.join();
                    logger->info("a thread has finished!");
                });
            }
        };
    }
}

#endif //C10K_SERVER_ROUND_ROBIN_POOL_HPP
