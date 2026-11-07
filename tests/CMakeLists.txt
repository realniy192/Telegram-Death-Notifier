cmake_minimum_required(VERSION 3.21)
project(TelegramDeathNotifierTests LANGUAGES CXX)

enable_testing()
add_executable(RunTrackingTests RunTrackingTests.cpp)
target_compile_features(RunTrackingTests PRIVATE cxx_std_17)
add_test(NAME RunTracking COMMAND RunTrackingTests)
