#pragma once
#include "io_stream.hpp"
#include "http.h"
class ip_loc_stream : public io::stream {
    char m_buffer[1024];
    size_t m_buffer_size;
    size_t m_buffer_start;
    esp_http_client_handle_t m_client;
    bool fill_buffer() {
        size_t data_read = esp_http_client_read(m_client, m_buffer, sizeof(m_buffer));
        if (data_read <= 0) {
            /*if (esp_http_client_is_complete_data_received(m_client)) {
                return false;
            } else {
                return false;
            }*/
            return false;
        }
        m_buffer_start = 0;
        m_buffer_size = data_read;
        return true;
    }
public:
    ip_loc_stream(esp_http_client_handle_t client) : m_buffer_size(0),m_buffer_start(0), m_client(client) {

    }
    virtual io::stream_caps caps() const override {
        io::stream_caps result;
        result.read = 1;
        result.seek = 0;
        result.write = 0;
        return result;
    }
    virtual int getch() override {
        if(m_buffer_start<m_buffer_size) {
            return m_buffer[m_buffer_start++];
        }
        if(!fill_buffer()) {
            return -1;
        }
        return m_buffer[m_buffer_start++];
    }
    virtual size_t read(uint8_t* data, size_t size) override {
        // Unexpected call to read() - not implemented
        // shouldn't be needed
        assert(false); // not implemented
        return 0;
    }
    virtual int putch(int value) override {
        return 0;
    }
    virtual size_t write(const uint8_t* data, size_t size) override {
        return 0;
    }
    virtual unsigned long long seek(long long position, io::seek_origin origin) override {
        return 0;
    }
};