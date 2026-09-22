cmake_minimum_required(VERSION 3.25)

project(kde_bluetooth_smartlocker VERSION 0.1.4 LANGUAGES CXX)

include(CTest)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.0 REQUIRED COMPONENTS Core DBus Qml)

add_library(smartlocker-state-machine STATIC)
target_compile_features(smartlocker-state-machine PUBLIC cxx_std_20)
target_include_directories(smartlocker-state-machine PUBLIC include)
target_sources(smartlocker-state-machine PRIVATE src/state_machine.cpp)
target_compile_options(smartlocker-state-machine PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(kde-bluetooth-smartlocker
    src/main.cpp
    src/daemon.cpp
    src/bluez_monitor.cpp
    include/smartlocker/daemon.hpp
    include/smartlocker/bluez_monitor.hpp
)
target_link_libraries(kde-bluetooth-smartlocker PRIVATE Qt6::Core Qt6::DBus smartlocker-state-machine)
target_compile_options(kde-bluetooth-smartlocker PRIVATE -Wall -Wextra -Wpedantic -Werror)
install(TARGETS kde-bluetooth-smartlocker RUNTIME DESTINATION bin)
add_library(smartlocker-qml-plugin MODULE src/qml_plugin.cpp)
target_link_libraries(smartlocker-qml-plugin PRIVATE Qt6::Core Qt6::DBus Qt6::Qml)
target_compile_options(smartlocker-qml-plugin PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(smartlocker-qml-plugin PROPERTIES LIBRARY_OUTPUT_NAME smartlockerqml)
install(TARGETS smartlocker-qml-plugin LIBRARY DESTINATION share/plasma/plasmoids/org.kde.smartlocker/contents/ui/org/kde/smartlocker)
configure_file(systemd/kde-bluetooth-smartlocker.service.in kde-bluetooth-smartlocker.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kde-bluetooth-smartlocker.service DESTINATION lib/systemd/user)
install(FILES plasmoid/metadata.json DESTINATION share/plasma/plasmoids/org.kde.smartlocker)
install(DIRECTORY plasmoid/contents DESTINATION share/plasma/plasmoids/org.kde.smartlocker)

if(BUILD_TESTING)
    add_executable(state-machine-test tests/state_machine_test.cpp)
    target_link_libraries(state-machine-test PRIVATE smartlocker-state-machine)
    target_compile_options(state-machine-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME state-machine-test COMMAND state-machine-test)
    add_test(NAME daemon-version COMMAND kde-bluetooth-smartlocker --version)
    add_test(NAME daemon-policy-options COMMAND kde-bluetooth-smartlocker --away-seconds 15 --minimum-present 1 --version)
    add_test(NAME daemon-zero-second-options COMMAND kde-bluetooth-smartlocker --away-seconds 0 --resume-grace-seconds 0 --version)
    add_test(NAME daemon-notification-option COMMAND kde-bluetooth-smartlocker --prelock-notify --version)
    add_test(NAME daemon-lock-command-option COMMAND kde-bluetooth-smartlocker --lock-command /usr/bin/loginctl --version)
    add_test(NAME daemon-invalid-device COMMAND kde-bluetooth-smartlocker --device invalid)
    set_tests_properties(daemon-invalid-device PROPERTIES WILL_FAIL TRUE)
    add_test(NAME daemon-duplicate-device COMMAND kde-bluetooth-smartlocker --device /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF
                                                        --device /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF)
    set_tests_properties(daemon-duplicate-device PROPERTIES WILL_FAIL TRUE)
    add_test(NAME daemon-minimum-present-exceeds-devices COMMAND kde-bluetooth-smartlocker --device /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF --minimum-present 2)
    set_tests_properties(daemon-minimum-present-exceeds-devices PROPERTIES WILL_FAIL TRUE)
endif()
