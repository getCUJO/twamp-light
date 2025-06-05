#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>

enum class ObservationPoints { CLIENT_SEND, SERVER_RECEIVE, SERVER_SEND, CLIENT_RECEIVE, NUM_OBSERVATION_POINTS };

class QEDObservation {
  private:
    ObservationPoints observation_point;
    uint64_t epoch_nanoseconds;
    uint32_t packet_id;
    uint16_t payload_len;

  public:
    QEDObservation(ObservationPoints observation_point,
                   uint64_t epoch_nanoseconds,
                   uint32_t packet_id,
                   uint16_t payload_len)
        : observation_point(observation_point), epoch_nanoseconds(epoch_nanoseconds), packet_id(packet_id),
          payload_len(payload_len)
    {
    }

    [[nodiscard]] auto getObservationPoint() const -> ObservationPoints
    {
        return observation_point;
    }
    [[nodiscard]] auto getEpochNanoseconds() const -> uint64_t
    {
        return epoch_nanoseconds;
    }
    [[nodiscard]] auto getPacketId() const -> uint32_t
    {
        return packet_id;
    }
    [[nodiscard]] auto getPayloadLen() const -> uint16_t
    {
        return payload_len;
    }
};

class ObservationList {
  private:
    std::deque<std::shared_ptr<QEDObservation>> observations;
    std::mutex mutex;

  public:
    ObservationList() = default;
    ~ObservationList() = default;
    ObservationList(const ObservationList &) = delete;
    auto operator=(const ObservationList &) -> ObservationList & = delete;
    ObservationList(ObservationList &&) = delete;
    auto operator=(ObservationList &&) -> ObservationList & = delete;

    [[nodiscard]] auto getObservations() const -> const std::deque<std::shared_ptr<QEDObservation>> &
    {
        return observations;
    }

    [[nodiscard]] auto isEmpty() const -> bool
    {
        return observations.empty();
    }
    void addObservation(const std::shared_ptr<QEDObservation> &observation)
    {
        std::unique_lock<std::mutex> lock(mutex);
        observations.push_back(observation);
    }

    auto popObservation() -> std::shared_ptr<QEDObservation>
    {
        std::unique_lock<std::mutex> lock(mutex);
        auto observation = observations.front();
        observations.pop_front();
        return observation;
    }
    [[nodiscard]] auto getOldestEntry() -> std::shared_ptr<QEDObservation>
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (observations.empty()) {
            return nullptr;
        }
        return observations.front();
    }
    [[nodiscard]] auto getSize() -> size_t
    {
        std::unique_lock<std::mutex> lock(mutex);
        return observations.size();
    }
    [[nodiscard]] auto begin() -> decltype(observations.begin())
    {
        return observations.begin();
    }
    [[nodiscard]] auto end() -> decltype(observations.end())
    {
        return observations.end();
    }
};