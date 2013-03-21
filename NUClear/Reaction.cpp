#include "Reaction.h"

namespace NUClear {

    std::atomic<int64_t> Reaction::idSource;
    
    Reaction::Reaction(std::function<void ()> callback, std::type_index type, reactionId_t parentId)
    : type(type), emitTime(std::time(nullptr)), reactionId(++Reaction::idSource), parentId(parentId) {
    }
    
    Reaction::~Reaction() {
    }
}
