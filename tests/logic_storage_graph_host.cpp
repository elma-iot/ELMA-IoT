#include "logic_storage_graph.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

class BudgetAllocator : public ArduinoJson::Allocator {
public:
    size_t limit = 8192, used = 0;
    std::map<void*, size_t> blocks;
    void* allocate(size_t size) override {
        if (used + size > limit) return nullptr;
        void* ptr = std::malloc(size);
        if (ptr) { used += size; blocks[ptr] = size; }
        return ptr;
    }
    void deallocate(void* ptr) override {
        if (!ptr) return;
        used -= blocks.at(ptr); blocks.erase(ptr); std::free(ptr);
    }
    void* reallocate(void* ptr, size_t size) override {
        if (!ptr) return allocate(size);
        size_t old = blocks.at(ptr);
        if (used - old + size > limit) return nullptr;
        void* next = std::realloc(ptr, size);
        if (next) { blocks.erase(ptr); blocks[next] = size; used = used - old + size; }
        return next;
    }
};

int main() {
    JsonDocument source;
    source["devices"] = std::string(20000, 'd');
    source["schemaVersion"] = 1;
    source["connections"].to<JsonArray>();
    source["groups"].to<JsonArray>();
    auto node = source["nodes"].to<JsonArray>().add<JsonObject>();
    node["id"] = "color"; node["type"] = "value.led_color";
    node["parameters"]["red"] = 150;
    node["ports"] = std::string(20000, 'p');
    node["binding"] = std::string(20000, 'b');
    BudgetAllocator budget;
    {
        JsonDocument compact(&budget);
        ElmaLogic::copyPersistedGraph(source.as<JsonVariantConst>(), compact.to<JsonObject>());
        assert(!compact.overflowed());
        assert(compact["devices"].isNull());
        assert(compact["nodes"][0]["ports"].isNull());
        assert(compact["nodes"][0]["binding"].isNull());
        assert(compact["nodes"][0]["parameters"]["red"] == 150);
        assert(compact["connections"].is<JsonArray>());
    }
    assert(budget.used == 0);
    budget.limit = 0;
    JsonDocument rejected(&budget);
    ElmaLogic::copyPersistedGraph(source.as<JsonVariantConst>(), rejected.to<JsonObject>());
    assert(rejected.overflowed());
    std::cout << "PASS: compact graph fits an 8 KiB allocation budget without copying 60 KiB of metadata; allocation failure remains detectable\n";
}
