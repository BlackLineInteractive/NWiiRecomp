#include "phone_source.h"
#include "runtime/config.h"
#include <cstdio>
#include <cstring>
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace nwii::runtime::input {

PhoneSource::PhoneSource() {
    int port = nwii::runtime::Config::get().phone_port;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    m_socket = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (m_socket < 0) {
        std::cerr << "[PhoneInput] Cannot create UDP socket" << std::endl;
        return;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);
    if (bind(m_socket, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[PhoneInput] Cannot bind UDP port " << port << std::endl;
#ifdef _WIN32
        closesocket(m_socket);
#else
        close(m_socket);
#endif
        m_socket = -1;
        return;
    }
#ifdef _WIN32
    u_long nonblock = 1;
    ioctlsocket(m_socket, FIONBIO, &nonblock);
#else
    fcntl(m_socket, F_SETFL, O_NONBLOCK);
#endif
    std::cout << "[PhoneInput] Listening on UDP port " << port
              << " (BTN/IR/ACC/GYR datagrams)" << std::endl;
}

PhoneSource::~PhoneSource() {
    if (m_socket >= 0) {
#ifdef _WIN32
        closesocket(m_socket);
#else
        close(m_socket);
#endif
    }
}

void PhoneSource::update(GameCubePadState pads[4], WiimoteState motes[4]) {
    if (m_socket < 0)
        return;

    auto& cfg = nwii::runtime::Config::get();
    char buf[256];

    // Drain all pending datagrams; the latest state wins
    while (true) {
#ifdef _WIN32
        int n = recv(m_socket, buf, sizeof(buf) - 1, 0);
#else
        ssize_t n = recv(m_socket, buf, sizeof(buf) - 1, 0);
#endif
        if (n <= 0)
            break;
        buf[n] = '\0';

        unsigned int mask;
        float a, b, c;
        if (std::sscanf(buf, "BTN %x", &mask) == 1) {
            m_buttons = mask;
        } else if (std::sscanf(buf, "IR %f %f", &a, &b) == 2) {
            m_ir_x = a;
            m_ir_y = b;
        } else if (std::sscanf(buf, "ACC %f %f %f", &a, &b, &c) == 3) {
            m_accel_x = a;
            m_accel_y = b;
            m_accel_z = c;
        } else if (std::sscanf(buf, "GYR %f %f %f", &a, &b, &c) == 3) {
            // Angular rate steers the pointer like a real Wiimote
            m_ir_x += b * 0.02f * cfg.gyro_sensitivity; // yaw
            m_ir_y += a * 0.02f * cfg.gyro_sensitivity; // pitch
            if (m_ir_x < 0.0f) m_ir_x = 0.0f;
            if (m_ir_x > 1.0f) m_ir_x = 1.0f;
            if (m_ir_y < 0.0f) m_ir_y = 0.0f;
            if (m_ir_y > 1.0f) m_ir_y = 1.0f;
        }
    }

    motes[0].err = 0;
    motes[0].buttons |= m_buttons;
    motes[0].ir_x = m_ir_x;
    motes[0].ir_y = m_ir_y;
    motes[0].accel_x = m_accel_x;
    motes[0].accel_y = m_accel_y;
    motes[0].accel_z = m_accel_z;

    // Mirror onto GC pad 0 so the phone drives GameCube titles too: the
    // BTN mask passes through raw (the sender uses the GC PAD layout,
    // e.g. START=0x1000, A=0x0100), the pointer maps to the main stick.
    pads[0].err = 0;
    pads[0].buttons |= (uint16_t)m_buttons;
    pads[0].stick_x = (int8_t)((m_ir_x - 0.5f) * 254.0f);
    pads[0].stick_y = (int8_t)((0.5f - m_ir_y) * 254.0f);
}

} // namespace nwii::runtime::input
