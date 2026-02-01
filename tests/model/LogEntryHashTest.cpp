#include <model/LogEntry.h>
#include <cassert>
#include <iostream>
#include <string>
#include <optional>
#include <unordered_set>
#include <unordered_map>

// Test function implementations
void testHashability() {
    std::cout << "Starting testHashability..." << std::endl;

    // Test LogValue hashability
    std::unordered_set<LogValue> value_set;
    LogValue lv1("hello");
    LogValue lv2(123LL);
    LogValue lv3("hello"); // Same as lv1
    LogValue lv4(std::chrono::nanoseconds(100));

    value_set.insert(lv1);
    value_set.insert(lv2);
    value_set.insert(lv3); // Should not insert a duplicate
    value_set.insert(lv4);

    assert(value_set.size() == 3);
    assert(value_set.count(lv1) == 1);
    assert(value_set.count(lv2) == 1);
    assert(value_set.count(lv3) == 1); // Found because it's equal to lv1
    assert(value_set.count(lv4) == 1);
    assert(value_set.count(LogValue(124LL)) == 0);

    // Test LogEntry hashability
    std::unordered_set<LogEntry> entry_set;
    LogEntry e1 = LogEntry::create(LogLevel::INFO, "message1");
    e1.withTag("tagA").withAttribute("attr1", "val1");

    LogEntry e2 = LogEntry::create(LogLevel::INFO, "message2");
    e2.withTag("tagB").withAttribute("attr2", static_cast<int64_t>(123LL));

    LogEntry e3 = e1; // Should be equal to e1

    entry_set.insert(e1);
    entry_set.insert(e2);
    entry_set.insert(e3); // Should not insert a duplicate

    assert(entry_set.size() == 2);
    assert(entry_set.count(e1) == 1);
    assert(entry_set.count(e2) == 1);
    assert(entry_set.count(e3) == 1); // Found because it's equal to e1

    // Test for different hash for different content
    LogEntry e4 = LogEntry::create(LogLevel::INFO, "message1");
    e4.withTag("tagA").withAttribute("attr1", "val2"); // Different attribute value
    assert(entry_set.count(e4) == 0); // Should not find e4 as it's different

    // Test with unordered_map
    std::unordered_map<LogEntry, int> entry_map;
    entry_map[e1] = 1;
    entry_map[e2] = 2;
    entry_map[e3] = 3; // Updates value for e1

    assert(entry_map.size() == 2);
    assert(entry_map.at(e1) == 3); // Value for e1 updated by e3
    assert(entry_map.at(e2) == 2);

    std::cout << "testHashability passed" << std::endl;
}


void runLogEntryHashTests() {
    testHashability();
    std::cout << "All LogEntry Hashability Tests Passed!" << std::endl;
}
