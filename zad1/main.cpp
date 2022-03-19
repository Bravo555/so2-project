#include <iostream>
#include <iomanip>
#include <thread>
#include <mutex>
#include <random>
#include <memory>
#include <SFML/Graphics.hpp>

const int WINDOW_WIDTH = 800, WINDOW_HEIGHT = 600;
const int TRACK_WIDTH = WINDOW_WIDTH / 2, TRACK_HEIGHT = WINDOW_HEIGHT / 2;
const float TRACK_THICKNESS = 100.0f;

const int CROSSTRACK_X = WINDOW_WIDTH * 0.5;

const float PATH_START_X = (WINDOW_WIDTH - TRACK_WIDTH - TRACK_THICKNESS) / 2;
const float PATH_START_Y = (WINDOW_HEIGHT - TRACK_HEIGHT - TRACK_THICKNESS) / 2;

const float PATH_END_X = (WINDOW_WIDTH - TRACK_THICKNESS * 1.5 );
const float PATH_END_Y = (WINDOW_HEIGHT - TRACK_THICKNESS * 1.0 );

const int FRAMETIME_INFO_PRINT_INTERVAL_MS = 1000;

const float CAR_SPEED = 5.0f;

namespace chrono = std::chrono;
using ms = std::chrono::duration<float, std::milli>;

enum CarMoveState {
    MOVE_RIGHT,
    MOVE_DOWN,
    MOVE_LEFT,
    MOVE_UP
};

struct Car {
    sf::RectangleShape shape;
    sf::Vector2f offset;
    CarMoveState state;

    Car(const sf::Vector2f& offset): offset(offset) {
        shape = sf::RectangleShape({2.0, 2.0});
        shape.setPosition({PATH_START_X, PATH_START_Y});
        shape.move(offset);
        shape.setOrigin({1.0, 1.0});
        state = MOVE_RIGHT;
    }

    void update() {
        auto pos = this->shape.getPosition();
        int newX = 0, newY = 0;

        int path_start_x = PATH_START_X + this->offset.x;
        int path_end_x = PATH_END_X + this->offset.x;
        int path_start_y = PATH_START_Y + this->offset.y;
        int path_end_y = PATH_END_Y + this->offset.y;

        switch (this->state) {
        case MOVE_RIGHT:
            newX = pos.x + CAR_SPEED;
            if(newX >= path_end_x) {
                this->state = MOVE_DOWN;
                newX = path_end_x;
            }
            this->shape.move({newX - pos.x, 0.0});
            break;

        case MOVE_DOWN:
            newY = pos.y + CAR_SPEED;
            if(newY >= path_end_y) {
                this->state = MOVE_LEFT;
                newY = path_end_y;
            }
            this->shape.move({0.0, newY - pos.y});
            break;

        case MOVE_LEFT:
            newX = pos.x - CAR_SPEED;
            if(newX <= path_start_x) {
                this->state = MOVE_UP;
                newX = path_start_x;
            }
            this->shape.move({newX - pos.x, 0.0});
            break;

        case MOVE_UP:
            newY = pos.y - CAR_SPEED;
            if(newY <= path_start_y) {
                this->state = MOVE_RIGHT;
                newX = path_start_y;
            }
            this->shape.move({0.0, newY - pos.y});
            break;
        
        default:
            break;
        }
    }
};


int main() {
    sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "Projekt Systemy operacyjne - zadanie 1");
    window.setFramerateLimit(120);

    sf::RectangleShape track({ TRACK_WIDTH, TRACK_HEIGHT });
    track.setOrigin(TRACK_WIDTH / 2, TRACK_HEIGHT / 2);
    track.setPosition(WINDOW_WIDTH / 2, WINDOW_HEIGHT / 2);
    track.setOutlineThickness(TRACK_THICKNESS);
    track.setOutlineColor(sf::Color::Blue);
    track.setFillColor(sf::Color::Transparent);

    sf::RectangleShape crossTrack({100, WINDOW_HEIGHT});
    crossTrack.setPosition(CROSSTRACK_X, 0.0);
    crossTrack.setFillColor(sf::Color({255, 150, 0, 150}));

    auto cars = std::make_shared<std::vector<Car>>();
    auto readCarsLock = std::make_shared<std::mutex>();

    auto handle = new std::thread([readCarsLock, cars] {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> car_offset_dist(-TRACK_THICKNESS / 4, TRACK_THICKNESS / 4);

        for(int i = 0; i < 100000; i++) {
            std::this_thread::sleep_for(chrono::microseconds(500));

            int x = car_offset_dist(gen);
            int y = car_offset_dist(gen);
            
            readCarsLock->lock();
            cars->emplace_back(Car({(float)x, (float)y}));
            readCarsLock->unlock();
        }
        std::cout << "exiting!" << std::endl;
    });
    handle->detach();

    auto programStartTimeMs = chrono::steady_clock::now();
    auto lastFrametimePrint = programStartTimeMs;
    uint32_t numFrame = 0;

    while (window.isOpen()) {
        ++numFrame;
        auto currentTime = chrono::steady_clock::now();

        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed)
                window.close();
        }

        readCarsLock->lock();

        // update
        auto frametimeUpdateStart = chrono::steady_clock::now();
        for(auto& car: *cars) {
            car.update();
        }
        auto frametimeUpdateEnd = chrono::steady_clock::now();

        // draw
        auto frametimeDrawStart = chrono::steady_clock::now();
        window.clear();
        window.draw(track);
        window.draw(crossTrack);

        for(auto& car: *cars) {
            window.draw(car.shape);
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
            std::cout << "frame: " << std::fixed << std::setw(5) << frametimeFull << " ms   ";
            std::cout << std::endl;
            lastFrametimePrint = currentTime;
        }
    }

    return 0;
}
