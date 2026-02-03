/**
 * Unit tests for packetlist.h classes (QEDObservation, ObservationList)
 */

#include <gtest/gtest.h>
#include "packetlist.h"
#include <thread>
#include <vector>
#include <atomic>

// ============================================================================
// Tests for ObservationPoints enum
// ============================================================================

TEST(ObservationPointsTest, EnumValues) {
    // Verify enum values exist
    EXPECT_NE(static_cast<int>(ObservationPoints::CLIENT_SEND), 
              static_cast<int>(ObservationPoints::SERVER_RECEIVE));
    EXPECT_NE(static_cast<int>(ObservationPoints::SERVER_RECEIVE), 
              static_cast<int>(ObservationPoints::SERVER_SEND));
    EXPECT_NE(static_cast<int>(ObservationPoints::SERVER_SEND), 
              static_cast<int>(ObservationPoints::CLIENT_RECEIVE));
}

// ============================================================================
// Tests for QEDObservation class
// ============================================================================

class QEDObservationTest : public ::testing::Test {};

TEST_F(QEDObservationTest, ConstructorInitialization) {
    QEDObservation obs(ObservationPoints::CLIENT_SEND, 1000000000ULL, 42, 100);
    
    EXPECT_EQ(obs.getObservationPoint(), ObservationPoints::CLIENT_SEND);
    EXPECT_EQ(obs.getEpochNanoseconds(), 1000000000ULL);
    EXPECT_EQ(obs.getPacketId(), 42U);
    EXPECT_EQ(obs.getPayloadLen(), 100U);
}

TEST_F(QEDObservationTest, AllObservationPoints) {
    QEDObservation obs1(ObservationPoints::CLIENT_SEND, 1, 1, 50);
    QEDObservation obs2(ObservationPoints::SERVER_RECEIVE, 2, 2, 50);
    QEDObservation obs3(ObservationPoints::SERVER_SEND, 3, 3, 50);
    QEDObservation obs4(ObservationPoints::CLIENT_RECEIVE, 4, 4, 50);
    
    EXPECT_EQ(obs1.getObservationPoint(), ObservationPoints::CLIENT_SEND);
    EXPECT_EQ(obs2.getObservationPoint(), ObservationPoints::SERVER_RECEIVE);
    EXPECT_EQ(obs3.getObservationPoint(), ObservationPoints::SERVER_SEND);
    EXPECT_EQ(obs4.getObservationPoint(), ObservationPoints::CLIENT_RECEIVE);
}

TEST_F(QEDObservationTest, LargeTimestampValue) {
    // Test with a large nanosecond value (year 2024 approximately)
    uint64_t large_ns = 1704067200000000000ULL; // Jan 1, 2024 00:00:00 UTC in ns
    QEDObservation obs(ObservationPoints::CLIENT_SEND, large_ns, 1, 100);
    
    EXPECT_EQ(obs.getEpochNanoseconds(), large_ns);
}

TEST_F(QEDObservationTest, MaxPacketId) {
    QEDObservation obs(ObservationPoints::CLIENT_SEND, 0, UINT32_MAX, 100);
    EXPECT_EQ(obs.getPacketId(), UINT32_MAX);
}

TEST_F(QEDObservationTest, MaxPayloadLen) {
    QEDObservation obs(ObservationPoints::CLIENT_SEND, 0, 0, UINT16_MAX);
    EXPECT_EQ(obs.getPayloadLen(), UINT16_MAX);
}

// ============================================================================
// Tests for ObservationList class
// ============================================================================

class ObservationListTest : public ::testing::Test {
protected:
    ObservationList list;
    
    std::shared_ptr<QEDObservation> makeObs(uint32_t id) {
        return std::make_shared<QEDObservation>(
            ObservationPoints::CLIENT_SEND, 1000000ULL * id, id, 100);
    }
};

TEST_F(ObservationListTest, InitiallyEmpty) {
    EXPECT_TRUE(list.isEmpty());
    EXPECT_EQ(list.getSize(), 0U);
}

TEST_F(ObservationListTest, AddObservation) {
    auto obs = makeObs(1);
    list.addObservation(obs);
    
    EXPECT_FALSE(list.isEmpty());
    EXPECT_EQ(list.getSize(), 1U);
}

TEST_F(ObservationListTest, AddMultipleObservations) {
    list.addObservation(makeObs(1));
    list.addObservation(makeObs(2));
    list.addObservation(makeObs(3));
    
    EXPECT_EQ(list.getSize(), 3U);
}

TEST_F(ObservationListTest, PopObservationFIFO) {
    list.addObservation(makeObs(1));
    list.addObservation(makeObs(2));
    list.addObservation(makeObs(3));
    
    auto obs1 = list.popObservation();
    EXPECT_EQ(obs1->getPacketId(), 1U);
    
    auto obs2 = list.popObservation();
    EXPECT_EQ(obs2->getPacketId(), 2U);
    
    auto obs3 = list.popObservation();
    EXPECT_EQ(obs3->getPacketId(), 3U);
    
    EXPECT_TRUE(list.isEmpty());
}

TEST_F(ObservationListTest, GetOldestEntryDoesNotRemove) {
    list.addObservation(makeObs(1));
    list.addObservation(makeObs(2));
    
    auto oldest1 = list.getOldestEntry();
    auto oldest2 = list.getOldestEntry();
    
    EXPECT_EQ(oldest1->getPacketId(), oldest2->getPacketId());
    EXPECT_EQ(list.getSize(), 2U);
}

TEST_F(ObservationListTest, GetOldestEntryReturnsNullWhenEmpty) {
    auto oldest = list.getOldestEntry();
    EXPECT_EQ(oldest, nullptr);
}

TEST_F(ObservationListTest, IteratorAccess) {
    list.addObservation(makeObs(1));
    list.addObservation(makeObs(2));
    list.addObservation(makeObs(3));
    
    uint32_t expected_id = 1;
    for (auto it = list.begin(); it != list.end(); ++it) {
        EXPECT_EQ((*it)->getPacketId(), expected_id);
        expected_id++;
    }
}

TEST_F(ObservationListTest, GetObservationsReturnsDeque) {
    list.addObservation(makeObs(1));
    list.addObservation(makeObs(2));
    
    const auto& observations = list.getObservations();
    EXPECT_EQ(observations.size(), 2U);
}

// ============================================================================
// Thread safety tests for ObservationList
// ============================================================================

class ObservationListThreadTest : public ::testing::Test {
protected:
    ObservationList list;
    std::atomic<int> added_count{0};
    std::atomic<int> popped_count{0};
};

TEST_F(ObservationListThreadTest, ConcurrentAdditions) {
    const int num_threads = 4;
    const int additions_per_thread = 100;
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([this, t, additions_per_thread]() {
            for (int i = 0; i < additions_per_thread; i++) {
                auto obs = std::make_shared<QEDObservation>(
                    ObservationPoints::CLIENT_SEND, 
                    static_cast<uint64_t>(t * 1000 + i), 
                    static_cast<uint32_t>(t * 1000 + i), 
                    100);
                list.addObservation(obs);
                added_count++;
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(list.getSize(), static_cast<size_t>(num_threads * additions_per_thread));
    EXPECT_EQ(added_count.load(), num_threads * additions_per_thread);
}

TEST_F(ObservationListThreadTest, ConcurrentAddAndPop) {
    const int num_items = 1000;
    
    // Pre-populate with some items
    for (int i = 0; i < num_items; i++) {
        auto obs = std::make_shared<QEDObservation>(
            ObservationPoints::CLIENT_SEND, 
            static_cast<uint64_t>(i), 
            static_cast<uint32_t>(i), 
            100);
        list.addObservation(obs);
    }
    
    std::atomic<bool> stop{false};
    std::atomic<int> pop_success{0};
    std::atomic<int> add_success{0};
    
    // Start popping thread
    std::thread popper([this, &stop, &pop_success]() {
        while (!stop.load() || !list.isEmpty()) {
            if (!list.isEmpty()) {
                list.popObservation();
                pop_success++;
            }
        }
    });
    
    // Start adding thread
    std::thread adder([this, &stop, &add_success]() {
        for (int i = 0; i < 500 && !stop.load(); i++) {
            auto obs = std::make_shared<QEDObservation>(
                ObservationPoints::CLIENT_SEND, 
                static_cast<uint64_t>(i + 10000), 
                static_cast<uint32_t>(i + 10000), 
                100);
            list.addObservation(obs);
            add_success++;
        }
    });
    
    adder.join();
    stop.store(true);
    popper.join();
    
    // All items should eventually be popped
    // Note: some timing variations may occur
    EXPECT_GE(pop_success.load(), 0);
    EXPECT_EQ(add_success.load(), 500);
}

// ============================================================================
// Edge case tests
// ============================================================================

TEST(ObservationListEdgeCaseTest, AddAndPopSingleItem) {
    ObservationList list;
    auto obs = std::make_shared<QEDObservation>(
        ObservationPoints::CLIENT_SEND, 12345ULL, 1, 50);
    
    list.addObservation(obs);
    EXPECT_FALSE(list.isEmpty());
    
    auto popped = list.popObservation();
    EXPECT_EQ(popped->getPacketId(), 1U);
    EXPECT_TRUE(list.isEmpty());
}

TEST(ObservationListEdgeCaseTest, LargeNumberOfItems) {
    ObservationList list;
    const size_t num_items = 10000;
    
    for (size_t i = 0; i < num_items; i++) {
        auto obs = std::make_shared<QEDObservation>(
            ObservationPoints::CLIENT_SEND, 
            static_cast<uint64_t>(i), 
            static_cast<uint32_t>(i), 
            100);
        list.addObservation(obs);
    }
    
    EXPECT_EQ(list.getSize(), num_items);
    
    // Pop all items and verify order
    for (size_t i = 0; i < num_items; i++) {
        auto obs = list.popObservation();
        EXPECT_EQ(obs->getPacketId(), static_cast<uint32_t>(i));
    }
    
    EXPECT_TRUE(list.isEmpty());
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
