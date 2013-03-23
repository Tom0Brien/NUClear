#include "ReactionQueue.h"

namespace NUClear {

    ReactionQueue::ReactionQueue() {
        // Intentionally left blank
    }

    ReactionQueue::~ReactionQueue() {
        // Intentionally left blank
    }

    void ReactionQueue::enqueue(std::unique_ptr<Reaction>&& reaction) {
        {
            std::unique_lock<std::mutex> lock(this->mutex);
            std::cout << reaction->reactionId << std::endl;
            queue.push_front(std::move(reaction));
        }
        this->condition.notify_one();
    }
    
    std::unique_ptr<Reaction> ReactionQueue::dequeue() {
        std::unique_lock<std::mutex> lock(this->mutex);
        this->condition.wait(lock, [this]() {
            std::cerr << "Queue empty: " << !this->queue.empty() << std::endl;
            return !this->queue.empty();
        });
        std::unique_ptr<Reaction> r = std::move(queue.back());
        std::cerr << "Reaction type name: " << r->type.name() << std::endl;
        queue.pop_back();
        return r;
    }
}
