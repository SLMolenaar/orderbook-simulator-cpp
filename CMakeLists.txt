cmake_minimum_required(VERSION 4.0)
project(OrderBookSim)

set(CMAKE_CXX_STANDARD 20)

add_executable(OrderBookSim main.cpp
        OrderType.h
        types.h
        LevelInfo.h
        Order.h
        OrderModify.h
        Trade.h
        OrderBook.h
        Constants.h)
