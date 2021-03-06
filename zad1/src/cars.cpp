#include <SFML/Graphics.hpp>

#include <unordered_set>
#include <chrono>
#include <iostream>
#include <optional>
#include <mutex>
#include <condition_variable>
#include <sstream>

namespace chrono = std::chrono;
using ms = std::chrono::duration<float, std::milli>;

const float CAR_SIZE = 20.0f;

template <typename T>
void printVecInline(const std::vector<T>& v) {
    for(auto& e: v) { std::cout << e << ' '; }
}

enum CarMoveState {
    MOVE_RIGHT,
    MOVE_DOWN,
    MOVE_LEFT,
    MOVE_UP,

    MOVE_STRAIGHT_DOWN
};

struct Car {
    uint32_t id;
    float speed;
    sf::RectangleShape shape;
    sf::Text label;
    sf::Vector2f offset;
    CarMoveState state;
    bool hasToken;

private:
    inline static uint32_t nextId = 0;

public:
    Car(const sf::Vector2f& offset, float speed): offset(offset), speed(speed) {
        shape = sf::RectangleShape({CAR_SIZE, CAR_SIZE});
        shape.setOrigin({CAR_SIZE / 2, CAR_SIZE / 2});
        hasToken = false;
        id = nextId++;
        label.setFillColor(sf::Color::Black);
        label.setString(std::to_string(id));
        label.setCharacterSize(12);
    }

    static Car spawnTrack(const sf::Vector2f& position, const sf::Vector2f& offset, float speed, const sf::Font& font) {
        Car car(offset, speed);

        car.shape.setPosition(position);
        car.shape.move(offset);
        car.label.setPosition(car.shape.getPosition() - sf::Vector2f{CAR_SIZE / 2, CAR_SIZE / 2});
        car.state = MOVE_RIGHT;

        car.label.setFont(font);

        return car;
    }

    static Car spawnCross(const sf::Vector2f& position, const sf::Vector2f& offset, float speed, const sf::Font& font) {
        Car car(offset, speed);

        car.shape.setPosition(position);
        car.shape.move(offset);
        car.state = MOVE_STRAIGHT_DOWN;

        car.label.setFont(font);

        return car;
    }
};

class SyncSystem {
public:
    std::vector<std::pair<uint32_t, CarMoveState>> givenTokens;
    static const int MAX_TOKENS = 4;
    std::condition_variable cv;
    std::mutex mutex;
    std::atomic<bool> exit;

    sf::Text passingVehiclesText;
    sf::RectangleShape passingVehiclesBackground;
    std::string passingVehiclesString;

    SyncSystem() {
        passingVehiclesBackground.setSize({100.0, 40.0});
        passingVehiclesBackground.setFillColor(sf::Color(255, 255, 255, 100));

        passingVehiclesText.setCharacterSize(14);
    }

    void setTextPosition(const sf::Vector2f& position) {
        passingVehiclesBackground.setPosition(position);
        passingVehiclesText.setPosition(position);
    }

    void draw(sf::RenderWindow& window) {
        window.draw(passingVehiclesBackground);
        window.draw(passingVehiclesText);
    }

    // Save all token requests into a sorted set. To grant a token, check if:
    // - it's at the index [0..MAX_TOKENS) after this token is released, next
    // - there are no elements with opposing state before in the queue
    // items will shift to the left and we'll be able to grant the token to the
    // next item in the waiting line immediately
    bool requestToken(const Car& car) {
        std::unique_lock lock(mutex);
        auto eqId = [&](const std::pair<uint32_t, CarMoveState>& pair) { return car.id == pair.first; };
        auto pos = std::find_if(givenTokens.begin(), givenTokens.end(), eqId);
        if(pos == std::end(givenTokens)) {
            auto& val = givenTokens.emplace_back(std::make_pair(car.id, car.state));
            pos = givenTokens.end() - 1;
        }

        auto firstCar = givenTokens.front();
        bool isQueuedBehindOpposingState = std::find_if(givenTokens.begin(), pos,
            [&](std::pair<uint32_t, CarMoveState>& pair){ return car.state != pair.second;}) != pos;
        auto matchPos = std::find_if(givenTokens.begin(), givenTokens.end(),
            [&](std::pair<uint32_t, CarMoveState>& pair){ return car.state != pair.second;});

        auto shouldPass = [&] {
            auto pos = std::find_if(givenTokens.begin(), givenTokens.end(), eqId);
            bool isQueuedBehindOpposingState = std::find_if(givenTokens.begin(), pos,
                [&](std::pair<uint32_t, CarMoveState>& pair){ return car.state != pair.second;}) != pos;
            auto shouldPass = std::distance(givenTokens.begin(), pos) < MAX_TOKENS
            && !isQueuedBehindOpposingState;
            return shouldPass || exit;
        };

        cv.wait(lock, shouldPass);

        // oh yes, just let me write 3 lines to interpolate a string
        // WHYYYYYY
        std::ostringstream ss;
        ss << car.id << " ";

        passingVehiclesString.append(ss.str());
        passingVehiclesText.setString(passingVehiclesString);
        return true;
    }

    bool releaseToken(const Car& car) {
        std::unique_lock lock(mutex);
        auto eqId = [&](const std::pair<uint32_t, CarMoveState>& pair) { return car.id == pair.first; };
        auto pos = std::find_if(givenTokens.begin(), givenTokens.end(), eqId);
        if(pos == std::end(givenTokens)) {
            return false;
        }
        givenTokens.erase(pos);

        std::ostringstream ss;
        ss << car.id << " ";
        auto strPos = passingVehiclesString.find(ss.str());
        if(strPos != passingVehiclesString.npos) {
            passingVehiclesString.erase(strPos, ss.str().length());
            passingVehiclesText.setString(passingVehiclesString);
        }

        cv.notify_all();

        return true;
    }
};

struct CarSystem {
    SyncSystem syncRegion0 = SyncSystem{};
    SyncSystem syncRegion1 = SyncSystem{};

    std::atomic<bool> exit;

    sf::FloatRect syncRegion0Box;
    sf::FloatRect syncRegion1Box;

    sf::FloatRect path;
    sf::Vector2f windowSize;

    CarSystem(const sf::FloatRect& path, const sf::Vector2f& syncPos0,
        const sf::Vector2f& syncPos1, const sf::Vector2f& syncSize,
        const sf::Vector2f windowSize, sf::Font& font) {
            syncRegion0Box = sf::Rect<float>(syncPos0, syncSize);
            syncRegion1Box = sf::Rect<float>(syncPos1, syncSize);

            syncRegion0.setTextPosition(syncPos0 + sf::Vector2f{syncSize.x, syncSize.y});
            syncRegion1.setTextPosition(syncPos1 + sf::Vector2f{syncSize.x, syncSize.y});

            syncRegion0.passingVehiclesText.setFont(font);
            syncRegion1.passingVehiclesText.setFont(font);

            this->path = path;
            this->windowSize = windowSize;
    }

    void shutdown() {
        exit = true;
        syncRegion0.exit = true;
        syncRegion1.exit = true;

        syncRegion0.cv.notify_all();
        syncRegion1.cv.notify_all();
    }

    void draw(sf::RenderWindow& window) {
        syncRegion0.draw(window);
        syncRegion1.draw(window);
    }

    std::unordered_set<size_t> removeSet;

    // TODO fix deleting on wrong indexes
    void update(std::vector<Car>& cars) {
        for(auto i = cars.begin(); i != cars.end(); ++i) {
            bool shouldRemove = updateCar(*i, false);
            if(shouldRemove) {
                size_t idx = i - cars.begin();
                removeSet.insert(idx);
            }
        }

            for(auto idx: removeSet) {
                cars.erase(cars.begin() + idx);
            }
            removeSet.clear();
        }

    void updateCarSync(Car& car) {
        while(!exit) {
            std::this_thread::sleep_for(chrono::microseconds(8333));
            updateCar(car, true);

            if(car.shape.getPosition().y > windowSize.y && car.state == MOVE_STRAIGHT_DOWN) {
                break;
            }
        }
        std::cout << "thread of car " << car.id << " exiting\n";
    }

    // Tries to synchronize access to sync regions using their request/release
    // Token methods. Returns true if car can move, and false if it can't.
    bool syncCrosses(Car& car, const sf::Vector2f& nextPosition, bool threadUpdate) {
        std::optional<std::reference_wrapper<SyncSystem>> syncRegion;
        bool canMove = true;
        if(syncRegion0Box.contains(nextPosition)) {
            syncRegion = syncRegion0;
        } else if(syncRegion1Box.contains(nextPosition)) {
            syncRegion = syncRegion1;
        } else {
            if(car.hasToken) {
                syncRegion0.releaseToken(car);
                syncRegion1.releaseToken(car);
                car.hasToken = false;
            }
        }

        if(syncRegion.has_value()) {
            car.hasToken = car.hasToken || syncRegion.value().get().requestToken(car);
            canMove = car.hasToken;
        }
        return canMove;
    }

    bool updateCar(Car& car, bool threadUpdate) {
        auto pos = car.shape.getPosition();
        float newX = 0, newY = 0;

        float path_start_x = path.left + car.offset.x;
        float path_end_x = path.left + path.width + car.offset.x;
        float path_start_y = path.top + car.offset.y;
        float path_end_y = path.top + path.height + car.offset.y;

        sf::Vector2f nextPosition;

        switch (car.state) {
        case MOVE_RIGHT:
            newX = pos.x + car.speed;
            if(newX >= path_end_x) {
                car.state = MOVE_DOWN;
                newX = path_end_x;
            }
            nextPosition = car.shape.getPosition() + sf::Vector2f{newX - pos.x, 0.0};
            break;

        case MOVE_DOWN:
            newY = pos.y + car.speed;
            if(newY >= path_end_y) {
                car.state = MOVE_LEFT;
                newY = path_end_y;
            }
            nextPosition = car.shape.getPosition() + sf::Vector2f{0.0, newY - pos.y};
            break;

        case MOVE_LEFT:
            newX = pos.x - car.speed;
            if(newX <= path_start_x) {
                car.state = MOVE_UP;
                newX = path_start_x;
            }
            nextPosition = car.shape.getPosition() + sf::Vector2f{newX - pos.x, 0.0};
            break;

        case MOVE_UP:
            newY = pos.y - car.speed;
            if(newY <= path_start_y) {
                car.state = MOVE_RIGHT;
                newX = path_start_y;
            }
            nextPosition = car.shape.getPosition() + sf::Vector2f{0.0, newY - pos.y};
            break;

        case MOVE_STRAIGHT_DOWN:
            newY = pos.y + car.speed;
            nextPosition = car.shape.getPosition() + sf::Vector2f{0.0, newY - pos.y};
            break;

        default:
            break;
        }

        // here check if we're trying to move on sync region
        bool canMove = true;
        canMove = syncCrosses(car, nextPosition, threadUpdate);

        if(canMove) {
            car.shape.setPosition(nextPosition);
            car.label.setPosition(nextPosition - sf::Vector2f{CAR_SIZE / 2, CAR_SIZE / 2});
        }

        if(nextPosition.y > windowSize.y) {
            return true;
        }
        return false;
    }
};
