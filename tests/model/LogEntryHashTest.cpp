#include "tests/model/LogEntryHashTest.h"
#include "model/LogEntry.h"
#include "model/LogValue.h"
#include <cassert>
#include <iostream>
#include <string>
#include <optional>
#include <unordered_set>
#include <unordered_map>
#include <chrono>

void testLogValueBasicHashability() {
    std::cout << "  Running testLogValueBasicHashability..." << std::endl;
    std::unordered_set<LogValue> valueSet;
    LogValue lv1("hello");
    LogValue lv2(123LL);
    LogValue lv3("hello"); // Same as lv1
    LogValue lv4(std::chrono::nanoseconds(100));

    valueSet.insert(lv1);
    valueSet.insert(lv2);
    valueSet.insert(lv3); // Should not insert a duplicate
    valueSet.insert(lv4);

    assert(valueSet.size() == 3);
    assert(valueSet.count(lv1) == 1);
    assert(valueSet.count(lv2) == 1);
    assert(valueSet.count(lv3) == 1); // Found because it's equal to lv1
    assert(valueSet.count(lv4) == 1);
    assert(valueSet.count(LogValue(124LL)) == 0);
    std::cout << "  testLogValueBasicHashability passed." << std::endl;
}

void testLogValueAllTypesHashing() {
    std::cout << "  Running testLogValueAllTypesHashing..." << std::endl;
    LogValue vStr("text");
    LogValue vInt(99LL);
    LogValue vDouble(3.14);
    LogValue vBool(true);
    LogValue vChrono(std::chrono::nanoseconds(12345));
    LogValue vNull(std::nullopt);

    std::unordered_set<LogValue> valueSet;
    valueSet.insert(vStr);
    valueSet.insert(vInt);
    valueSet.insert(vDouble);
    valueSet.insert(vBool);
    valueSet.insert(vChrono);
    valueSet.insert(vNull);

    assert(valueSet.size() == 6);
    assert(valueSet.count(vStr) == 1);
    assert(valueSet.count(LogValue("text")) == 1);
    assert(valueSet.count(LogValue("different_text")) == 0);

    assert(valueSet.count(vInt) == 1);
    assert(valueSet.count(LogValue(99LL)) == 1);
    assert(valueSet.count(LogValue(100LL)) == 0);

    assert(valueSet.count(vDouble) == 1);
    assert(valueSet.count(LogValue(3.14)) == 1);
    assert(valueSet.count(LogValue(3.15)) == 0);

    assert(valueSet.count(vBool) == 1);
    assert(valueSet.count(LogValue(true)) == 1);
    assert(valueSet.count(LogValue(false)) == 0);

    assert(valueSet.count(vChrono) == 1);
    assert(valueSet.count(std::chrono::nanoseconds(12345)) == 1);
    assert(valueSet.count(std::chrono::nanoseconds(54321)) == 0);
    
    assert(valueSet.count(vNull) == 1);
    assert(valueSet.count(LogValue(std::nullopt)) == 1);

    std::hash<LogValue> hasher;
    assert(hasher(vStr) != hasher(LogValue("different text")));
    assert(hasher(vInt) != hasher(LogValue(100LL)));
    assert(hasher(vDouble) != hasher(LogValue(1.23)));
    assert(hasher(vBool) != hasher(LogValue(false)));
    assert(hasher(vChrono) != hasher(std::chrono::nanoseconds(999)));
    assert(hasher(vNull) != hasher(vStr));

    std::cout << "  testLogValueAllTypesHashing passed." << std::endl;
}

void testLogEntryCoreFieldHashing() {
    std::cout << "  Running testLogEntryCoreFieldHashing..." << std::endl;
    LogEntry e1 = LogEntry::create(LogLevel::INFO, "message");
    LogEntry e2 = LogEntry::create(e1.time_point + std::chrono::nanoseconds(1), LogLevel::INFO, "message");
    LogEntry e3 = LogEntry::create(e1.time_point, LogLevel::WARNING, "message");
    LogEntry e4 = LogEntry::create(e1.time_point, LogLevel::INFO, "different message");

    std::hash<LogEntry> hasher;
    size_t h1 = hasher(e1);
    size_t h2 = hasher(e2);
    size_t h3 = hasher(e3);
    size_t h4 = hasher(e4);

    assert(h1 != h2); // Different timestamp
    assert(h1 != h3); // Different level
    assert(h1 != h4); // Different message
    std::cout << "  testLogEntryCoreFieldHashing passed." << std::endl;
}

void testLogEntryTagHashing() {
    std::cout << "  Running testLogEntryTagHashing..." << std::endl;
    LogEntry base = LogEntry::create(LogLevel::INFO, "message");
    LogEntry withTagA = LogEntry(base).withTag("tagA");
    LogEntry withTagB = LogEntry(base).withTag("tagB");
    LogEntry withTagsAB = LogEntry(base).withTag("tagA").withTag("tagB");
    LogEntry withTagsBA = LogEntry(base).withTag("tagB").withTag("tagA");

    std::hash<LogEntry> hasher;
    size_t hBase = hasher(base);
    size_t hA = hasher(withTagA);
    size_t hB = hasher(withTagB);
    size_t hAB = hasher(withTagsAB);
    size_t hBA = hasher(withTagsBA);

    assert(hBase != hA);
    assert(hBase != hB);
    assert(hA != hB);
    assert(hA != hAB);
    assert(hB != hAB);
    assert(hAB == hBA); // Order of tags should not matter
    std::cout << "  testLogEntryTagHashing passed." << std::endl;
}

void testLogEntryAttributeHashing() {
    std::cout << "  Running testLogEntryAttributeHashing..." << std::endl;
    LogEntry base = LogEntry::create(LogLevel::INFO, "message");
    LogEntry withAttr1 = LogEntry(base).withAttribute("key1", "val1");
    LogEntry withAttr2 = LogEntry(base).withAttribute("key2", "val2");
    LogEntry withDifferentVal = LogEntry(base).withAttribute("key1", "valX");
    LogEntry withAttrs12 = LogEntry(base).withAttribute("key1", "val1").withAttribute("key2", "val2");
    LogEntry withAttrs21 = LogEntry(base).withAttribute("key2", "val2").withAttribute("key1", "val1");

    std::hash<LogEntry> hasher;
    size_t hBase = hasher(base);
    size_t h1 = hasher(withAttr1);
    size_t h2 = hasher(withAttr2);
    size_t hDiff = hasher(withDifferentVal);
    size_t h12 = hasher(withAttrs12);
    size_t h21 = hasher(withAttrs21);

    assert(hBase != h1);
    assert(hBase != h2);
    assert(h1 != h2);
    assert(h1 != hDiff);
    assert(h1 != h12);
    assert(h2 != h12);
    assert(h12 == h21); // Order of attributes should not matter
    std::cout << "  testLogEntryAttributeHashing passed." << std::endl;
}

void testLogEntryContextHashing() {
    std::cout << "  Running testLogEntryContextHashing..." << std::endl;

    LogEntry base = LogEntry::create(LogLevel::INFO, "message")
                              .withHost("host1")
                              .withApp("app1")
                              .withProcessId(100);

    LogEntry e1 = base;
    LogEntry e2 = LogEntry(base).withHost("host2");
    LogEntry e3 = LogEntry(base).withApp("app2");
    LogEntry e4 = LogEntry(base).withProcessId(200);
    LogEntry e5 = base; // same as e1

    std::hash<LogEntry> hasher;
    size_t h1 = hasher(e1);
    size_t h2 = hasher(e2);
    size_t h3 = hasher(e3);
    size_t h4 = hasher(e4);
    size_t h5 = hasher(e5);

    assert(h1 != h2); // Different hostname
    assert(h1 != h3); // Different appName
    assert(h1 != h4); // Different processId
    assert(h1 == h5); // Identical context
    std::cout << "  testLogEntryContextHashing passed." << std::endl;
}

void testLogEntryComplexScenarioHashing() {
    std::cout << "  Running testLogEntryComplexScenarioHashing..." << std::endl;
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
    std::cout << "  testLogEntryComplexScenarioHashing passed." << std::endl;
}

void runLogEntryHashTests() {
    std::cout << "Starting LogEntry Hash Tests..." << std::endl;

    testLogValueBasicHashability();
    testLogValueAllTypesHashing();
    testLogEntryCoreFieldHashing();
    testLogEntryTagHashing();
    testLogEntryAttributeHashing();
    testLogEntryContextHashing();
    testLogEntryComplexScenarioHashing();

    std::cout << "All LogEntry Hash Tests Passed!" << std::endl;
}
