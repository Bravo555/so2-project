#include <iostream>
#include <iomanip>
#include <thread>
#include <mutex>
#include <random>
#include <memory>
#include <queue>
#include <optional>
#include <SFML/Graphics.hpp>

const int WINDOW_WIDTH = 800, WINDOW_HEIGHT = 600;
const int TRACK_WIDTH = WINDOW_WIDTH / 2, TRACK_HEIGHT = WINDOW_HEIGHT / 2;
const float TRACK_THICKNESS = 100.0f;

const float PATH_START_X = (WINDOW_WIDTH - TRACK_WIDTH - TRACK_THICKNESS) / 2;
const float PATH_START_Y = (WINDOW_HEIGHT - TRACK_HEIGHT - TRACK_THICKNESS) / 2;

const float PATH_END_X = (WINDOW_WIDTH - TRACK_THICKNESS * 1.5 );
const float PATH_END_Y = (WINDOW_HEIGHT - TRACK_THICKNESS * 1.0 );

const int CROSSTRACK_X = WINDOW_WIDTH * 0.5;
const int CROSSTRACK_WIDTH = 100;

const float SYNC_REGION0_Y = (WINDOW_HEIGHT / 2) - (TRACK_HEIGHT / 2) - TRACK_THICKNESS;
const float SYNC_REGION1_Y = (WINDOW_HEIGHT / 2) + (TRACK_HEIGHT / 2);

const int SYNC_REGION_WIDTH = CROSSTRACK_WIDTH;
const float SYNC_REGION_HEIGHT = TRACK_THICKNESS;

const int FRAMETIME_INFO_PRINT_INTERVAL_MS = 1000;

const float CAR_SPEED = 2.0f;
const float CAR_SIZE = 20.0f;

const int NUM_CARS = 100;

namespace chrono = std::chrono;
using ms = std::chrono::duration<float, std::milli>;

class SyncSystem {
public:
    std::vector<uint32_t> givenTokens;
    static const int MAX_TOKENS = 4;

    // save all token requests into a sorted set. to grant a token, check if
    // it's at the index [0..MAX_TOKENS). after this token is released, next
    // items will shift to the left and we'll be able to grant the token to the
    // next item in the waiting line immediately
    bool requestToken(uint32_t id) {
        if(std::find(givenTokens.begin(), givenTokens.end(), id) == std::end(givenTokens)) {
            givenTokens.emplace_back(id);
        }

        auto pos = std::find(givenTokens.begin(), givenTokens.end(), id);
        if(pos == std::end(givenTokens)) {
            return false;
        }

        return std::distance(givenTokens.begin(), pos) < MAX_TOKENS;
    }

    bool releaseToken(uint32_t id) {
        auto pos = std::find(givenTokens.begin(), givenTokens.end(), id);
        if(pos != std::end(givenTokens)) {
            givenTokens.erase(pos);
            return true;
        }
        return false;
    }
};

enum CarMoveState {
    MOVE_RIGHT,
    MOVE_DOWN,
    MOVE_LEFT,
    MOVE_UP,

    MOVE_STRAIGHT_DOWN
};

struct Car {
    uint32_t id;
    sf::RectangleShape shape;
    sf::Text label;
    sf::Vector2f offset;
    CarMoveState state;
    bool hasToken;

private:
    inline static uint32_t nextId = 0;

public:
    Car(const sf::Vector2f& offset): offset(offset) {
        shape = sf::RectangleShape({CAR_SIZE, CAR_SIZE});
        shape.setOrigin({CAR_SIZE / 2, CAR_SIZE / 2});
        hasToken = false;
        id = nextId++;
        label.setFillColor(sf::Color::Black);
        label.setString(std::to_string(id));
        label.setCharacterSize(14);
    }

    static Car spawnTrack(const sf::Vector2f& offset) {
        Car car(offset);

        car.shape.setPosition({PATH_START_X, PATH_START_Y});
        car.shape.move(offset);
        car.label.setPosition(car.shape.getPosition() - sf::Vector2f{CAR_SIZE / 2, CAR_SIZE / 2});
        car.state = MOVE_RIGHT;

        return car;
    }

    static Car spawnCross(const sf::Vector2f& offset) {
        Car car(offset);

        car.shape.setPosition({CROSSTRACK_X + (CROSSTRACK_WIDTH / 2), 0});
        car.shape.move(offset);
        car.state = MOVE_STRAIGHT_DOWN;

        return car;
    }
};

struct CarSystem {
    SyncSystem syncRegion0;
    SyncSystem syncRegion1;

    void update(Car& car) {
        auto pos = car.shape.getPosition();
        int newX = 0, newY = 0;

        int path_start_x = PATH_START_X + car.offset.x;
        int path_end_x = PATH_END_X + car.offset.x;
        int path_start_y = PATH_START_Y + car.offset.y;
        int path_end_y = PATH_END_Y + car.offset.y;

        sf::Vector2f nextPosition;

        switch (car.state) {
        case MOVE_RIGHT:
            newX = pos.x + CAR_SPEED;
            if(newX >= path_end_x) {
                car.state = MOVE_DOWN;
                newX = path_end_x;
            }
            nextPosition = car.shape.getPosition() + sf::Vector2f{newX - pos.x, 0.0};
            break;

        case MOVE_DOWN:
            newY = pos.y + CAR_SPEED;
            if(newY >= path_end_y) {
                car.state = MOVE_LEFT;
                newY = path_end_y;
            }
            nextPosition = car.shape.getPosition() + sf::Vector2f{0.0, newY - pos.y};
            break;

        case MOVE_LEFT:
            newX = pos.x - CAR_SPEED;
            if(newX <= path_start_x) {
                car.state = MOVE_UP;
                newX = path_start_x;
            }
            nextPosition = car.shape.getPosition() + sf::Vector2f{newX - pos.x, 0.0};
            break;

        case MOVE_UP:
            newY = pos.y - CAR_SPEED;
            if(newY <= path_start_y) {
                car.state = MOVE_RIGHT;
                newX = path_start_y;
            }
            nextPosition = car.shape.getPosition() + sf::Vector2f{0.0, newY - pos.y};
            break;

        default:
            break;
        }

        // here check if we're trying to move on sync region
        auto syncRegion0Box = sf::Rect<float>({CROSSTRACK_X, SYNC_REGION0_Y}, {SYNC_REGION_WIDTH, SYNC_REGION_HEIGHT});
        auto syncRegion1Box = sf::Rect<float>({CROSSTRACK_X, SYNC_REGION1_Y}, {SYNC_REGION_WIDTH, SYNC_REGION_HEIGHT});

        std::optional<std::reference_wrapper<SyncSystem>> syncRegion;
        if(syncRegion0Box.contains(nextPosition)) {
            syncRegion = syncRegion0;
        } else if(syncRegion1Box.contains(nextPosition)) {
            syncRegion = syncRegion1;
        } else {
            car.shape.setPosition(nextPosition);
            car.label.setPosition(nextPosition - sf::Vector2f{CAR_SIZE / 2, CAR_SIZE / 2});
            if(car.hasToken) {
                syncRegion0.releaseToken(car.id);
                syncRegion1.releaseToken(car.id);
                car.hasToken = false;
            }
            return;
        }

        car.hasToken = car.hasToken || syncRegion.value().get().requestToken(car.id);
        if(car.hasToken) {
            car.shape.setPosition(nextPosition);
            car.label.setPosition(nextPosition - sf::Vector2f{CAR_SIZE / 2, CAR_SIZE / 2});
        }
    }
};


template <typename T>
void printVecInline(const std::vector<T>& v) {
    for(auto& e: v) { std::cout << e << ' '; }
}


int main() {
    sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "Projekt Systemy operacyjne - zadanie 1");
    window.setFramerateLimit(120);

    sf::Font font;
    font.loadFromFile("/usr/share/fonts/TTF/Roboto-Regular.ttf");

    sf::RectangleShape track({ TRACK_WIDTH, TRACK_HEIGHT });
    track.setOrigin(TRACK_WIDTH / 2, TRACK_HEIGHT / 2);
    track.setPosition(WINDOW_WIDTH / 2, WINDOW_HEIGHT / 2);
    track.setOutlineThickness(TRACK_THICKNESS);
    track.setOutlineColor(sf::Color::Blue);
    track.setFillColor(sf::Color::Transparent);

    sf::RectangleShape crossTrack({CROSSTRACK_WIDTH, WINDOW_HEIGHT});
    crossTrack.setPosition(CROSSTRACK_X, 0.0);
    crossTrack.setFillColor(sf::Color(sf::Color::Blue));

    sf::RectangleShape syncRegion0({SYNC_REGION_WIDTH, SYNC_REGION_HEIGHT});
    syncRegion0.setPosition(CROSSTRACK_X, SYNC_REGION0_Y);
    syncRegion0.setFillColor(sf::Color::Red);

    sf::RectangleShape syncRegion1({SYNC_REGION_WIDTH, SYNC_REGION_HEIGHT});
    syncRegion1.setPosition(CROSSTRACK_X, SYNC_REGION1_Y);
    syncRegion1.setFillColor(sf::Color::Red);

    auto cars = std::make_shared<std::vector<Car>>();
    auto readCarsLock = std::make_shared<std::mutex>();
    auto carSystem = CarSystem { SyncSystem{}, SyncSystem{} };
    auto pause = std::make_shared<std::atomic<bool>>(false);

    auto handle = new std::thread([readCarsLock, cars, pause, &font] {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> car_offset_dist(-TRACK_THICKNESS / 4, TRACK_THICKNESS / 4);

        for(int i = 0; i < NUM_CARS; !*pause ? ++i : i) {
            std::this_thread::sleep_for(chrono::milliseconds(100));
            if(*pause) continue;

            int x = car_offset_dist(gen);
            int y = car_offset_dist(gen);

            readCarsLock->lock();
            auto& c = cars->emplace_back(Car::spawnTrack({(float)x, (float)y}));
            c.label.setFont(font);
            readCarsLock->unlock();
        }
        std::cout << "exiting!" << std::endl;
    });
    handle->detach();

    auto programStartTimeMs = chrono::steady_clock::now();
    auto lastFrametimePrint = programStartTimeMs;
    uint32_t numFrame = 0;

    while (window.isOpen()) {
        auto currentTime = chrono::steady_clock::now();

        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed)
                window.close();

            if(event.type == sf::Event::KeyPressed) {
                if(event.key.code == sf::Keyboard::P) {
                    *pause = !*pause;
                    lastFrametimePrint = programStartTimeMs;
                }
            }
        }

        if(*pause) continue;
        ++numFrame;

        readCarsLock->lock();

        // update
        auto frametimeUpdateStart = chrono::steady_clock::now();
        for(auto& car: *cars) {
            carSystem.update(car);
        }
        auto frametimeUpdateEnd = chrono::steady_clock::now();

        // draw
        auto frametimeDrawStart = chrono::steady_clock::now();
        window.clear();
        window.draw(track);
        window.draw(crossTrack);
        window.draw(syncRegion0);
        window.draw(syncRegion1);

        for(auto& car: *cars) {
            window.draw(car.shape);
            window.draw(car.label);
        }

        sf::Vertex lines[] = {
            sf::Vertex({0.0, SYNC_REGION0_Y}), sf::Vertex({WINDOW_WIDTH, SYNC_REGION0_Y}),
            sf::Vertex({0.0, SYNC_REGION1_Y}), sf::Vertex({WINDOW_WIDTH, SYNC_REGION1_Y})
        };

        window.draw(lines, 4, sf::Lines);

        auto frametimeDrawEnd = chrono::steady_clock::now();

        readCarsLock->unlock();

        window.display();

        float frametimeDraw = chrono::duration_cast<ms>(frametimeDrawEnd - frametimeDrawStart).count();
        float frametimeUpdate = chrono::duration_cast<ms>(frametimeUpdateEnd - frametimeUpdateStart).count();
        float frametimeFull = chrono::duration_cast<ms>(chrono::steady_clock::now() - currentTime).count();

        if(chrono::duration_cast<ms>(currentTime - lastFrametimePrint).count() > FRAMETIME_INFO_PRINT_INTERVAL_MS) {
            std::cout.precision(3);
            std::cout << '[' << numFrame << ']' << "   ";
            std::cout << "simulation: " << std::fixed << std::setw(5) << frametimeUpdate << " ms   ";
            std::cout << "draw: " << std::fixed << std::setw(5) << frametimeDraw << " ms   ";
            std::cout << "frame: " << std::fixed << std::setw(5) << frametimeFull << " ms   \n";

            lastFrametimePrint = currentTime;
        }
    }

    return 0;
}
