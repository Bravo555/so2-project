#include <iostream>
#include <iomanip>
#include <thread>
#include <random>
#include <memory>
#include <queue>
#include <unordered_set>
#include <vector>
#include <optional>
#include <SFML/Graphics.hpp>

#include "cars.cpp"

const int WINDOW_WIDTH = 800, WINDOW_HEIGHT = 600;
const int TRACK_WIDTH = WINDOW_WIDTH / 2, TRACK_HEIGHT = WINDOW_HEIGHT / 2;
const float TRACK_THICKNESS = 100.0f;

const float PATH_START_X = (WINDOW_WIDTH - TRACK_WIDTH - TRACK_THICKNESS) / 2;
const float PATH_START_Y = (WINDOW_HEIGHT - TRACK_HEIGHT - TRACK_THICKNESS) / 2;

const float PATH_END_X = (WINDOW_WIDTH - TRACK_THICKNESS * 1.5 );
const float PATH_END_Y = (WINDOW_HEIGHT - TRACK_THICKNESS * 1.0 );

const float PATH_SIZE_X = PATH_END_X - PATH_START_X;
const float PATH_SIZE_Y = PATH_END_Y - PATH_START_Y;

const int CROSSTRACK_X = WINDOW_WIDTH * 0.5;
const int CROSSTRACK_WIDTH = 100;

const float SYNC_REGION0_Y = (WINDOW_HEIGHT / 2) - (TRACK_HEIGHT / 2) - TRACK_THICKNESS;
const float SYNC_REGION1_Y = (WINDOW_HEIGHT / 2) + (TRACK_HEIGHT / 2);

const float SYNC_REGION_WIDTH = CROSSTRACK_WIDTH;
const float SYNC_REGION_HEIGHT = TRACK_THICKNESS;

const int FRAMETIME_INFO_PRINT_INTERVAL_MS = 1000;

const float CAR_SPEED_MIN = 0.5f;
const float CAR_SPEED_MAX = 2.0f;

const int NUM_CARS = 20;

const bool THREAD_UPDATE = true;

namespace chrono = std::chrono;
using ms = std::chrono::duration<float, std::milli>;


int main() {
    sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "Projekt Systemy operacyjne - zadanie 1");
    window.setFramerateLimit(120);

    sf::Font font;
    font.loadFromFile("/usr/share/fonts/TTF/DejaVuSansMono.ttf");

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
    auto carSystem = std::shared_ptr<CarSystem>(new CarSystem{
        {PATH_START_X, PATH_START_Y, PATH_SIZE_X, PATH_SIZE_Y},
        {CROSSTRACK_X, SYNC_REGION0_Y},
        {CROSSTRACK_X, SYNC_REGION1_Y},
        {SYNC_REGION_WIDTH, SYNC_REGION_HEIGHT},
        {WINDOW_WIDTH, WINDOW_HEIGHT},
        font
    });

    auto pause = std::make_shared<std::atomic<bool>>(false);
    std::vector<std::jthread*> handles;

    auto threadedCarsLock = std::make_shared<std::mutex>();
    std::vector<std::shared_ptr<Car>> threadedCars;

    auto spawnTrack = new std::jthread([&, readCarsLock, cars, pause] {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> car_offset_dist(-TRACK_THICKNESS / 4, TRACK_THICKNESS / 4);
        std::uniform_real_distribution<float> speed_dist(CAR_SPEED_MIN, CAR_SPEED_MAX);
        std::uniform_int_distribution<int> nextSpawnTimeMsDist(100, 1000);

        for(int i = 0; i < NUM_CARS; !*pause ? ++i : i) {
            auto nextSpawnTimeMs = nextSpawnTimeMsDist(gen);
            std::this_thread::sleep_for(chrono::milliseconds(nextSpawnTimeMs));
            if(*pause) continue;

            float x = car_offset_dist(gen);
            float y = car_offset_dist(gen);
            float speed = speed_dist(gen);

            if(!THREAD_UPDATE) {
                readCarsLock->lock();
                auto& c = cars->emplace_back(Car::spawnTrack({PATH_START_X, PATH_START_Y}, {x, y}, speed, font));
                readCarsLock->unlock();
            } else {
                handles.emplace_back(new std::jthread([&](){
                    auto c = std::make_shared<Car>(Car::spawnTrack({PATH_START_X, PATH_START_Y}, {x, y}, speed, font));
                    threadedCarsLock->lock();
                    threadedCars.push_back(c);
                    threadedCarsLock->unlock();
                    carSystem->updateCarSync(*c);
                }));
            }
        
            if(carSystem->exit) {
                return;
            }
        }
        std::cout << "spawn thread exiting!" << std::endl;
    });

    auto spawnCrosstrack = new std::jthread([&, readCarsLock, cars, pause] {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> car_offset_dist(-TRACK_THICKNESS / 4, TRACK_THICKNESS / 4);
        std::uniform_real_distribution<float> speed_dist(CAR_SPEED_MIN, CAR_SPEED_MAX);
        std::uniform_int_distribution<int> nextSpawnTimeMsDist(100, 1000);

        while(!carSystem->exit) {
            auto nextSpawnTimeMs = nextSpawnTimeMsDist(gen);
            std::this_thread::sleep_for(chrono::milliseconds(nextSpawnTimeMs));
            if(*pause) continue;

            float x = car_offset_dist(gen);
            float y = car_offset_dist(gen);
            float speed = speed_dist(gen);

            if(!THREAD_UPDATE) {
                readCarsLock->lock();
                auto& c = cars->emplace_back(Car::spawnCross({CROSSTRACK_X + (CROSSTRACK_WIDTH / 2), 0}, {x, y}, speed, font));
                readCarsLock->unlock();
            } else {
                handles.emplace_back(new std::jthread([&](){
                    auto c = std::make_shared<Car>(Car::spawnCross({CROSSTRACK_X + (CROSSTRACK_WIDTH / 2), 0}, {x, y}, speed, font));

                    threadedCarsLock->lock();
                    threadedCars.end();
                    threadedCars.push_back(c);
                    threadedCarsLock->unlock();

                    carSystem->updateCarSync(*c);

                    threadedCarsLock->lock();
                    auto pos = std::find(threadedCars.begin(), threadedCars.end(), c);
                    if(pos != threadedCars.end())
                        threadedCars.erase(pos);
                    threadedCarsLock->unlock();
                }));
            }
        }
        std::cout << "exiting spawn thread!" << std::endl;
    });

    auto programStartTimeMs = chrono::steady_clock::now();
    auto lastFrametimePrint = programStartTimeMs;
    uint32_t numFrame = 0;

    while (window.isOpen()) {
        auto currentTime = chrono::steady_clock::now();

        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed) {
                carSystem->shutdown();
                window.close();
            }

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
        if(!THREAD_UPDATE) carSystem->update(*cars);
        auto frametimeUpdateEnd = chrono::steady_clock::now();

        // draw
        auto frametimeDrawStart = chrono::steady_clock::now();

        window.clear();
        window.draw(track);
        window.draw(crossTrack);
        window.draw(syncRegion0);
        window.draw(syncRegion1);

        carSystem->draw(window);

        if(!THREAD_UPDATE) {
            for(auto& car: *cars) {
                window.draw(car.shape);
                window.draw(car.label);
            }
        } else {
            threadedCarsLock->lock();
            for(auto& car: threadedCars) {
                window.draw(car->shape);
                window.draw(car->label);
            }
            threadedCarsLock->unlock();
        }

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
