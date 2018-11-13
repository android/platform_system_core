/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define TRACE_TAG TRANSPORT

#include "connection.h"

#include "sysdeps.h"

#include <mutex>
#include <thread>

#include <android-base/stringprintf.h>
#include <android-base/thread_annotations.h>

#include "adb.h"
#include "adb_io.h"
#include "adb_trace.h"
#include "adb_utils.h"

using android::base::StringPrintf;

void Connection::HandleError(const char* fmt, ...) {
    std::string message;
    va_list va;
    va_start(va, fmt);
    android::base::StringAppendV(&message, fmt, va);
    va_end(va);

    std::call_once(error_flag_, [this, msg{std::move(message)}]() {
        error_callback_(this, msg);
        Stop();
    });
}

bool PacketConnection::HandlePacket(Block&& packet) {
    if (packet.empty()) return true;

    if (!this->header_.has_value()) {
        if (packet.size() != sizeof(amessage)) {
            HandleError("received packet of size %zu, expected header of size %zu", packet.size(),
                        sizeof(amessage));
            return false;
        }
        amessage msg;
        memcpy(&msg, packet.data(), packet.size());
        header_ = msg;

        LOG(VERBOSE) << this->transport_name_ << " <<< header " << dump_header(&msg);
    } else {
        size_t bytes_left = this->header_->data_length - payload_.size();
        if (packet.size() > bytes_left) {
            HandleError(
                    "received too many bytes while reading packet data: wanted %zu more, got %zu",
                    bytes_left, packet.size());
            return false;
        }

        payload_.append(std::move(packet));
    }

    // We're guaranteed to have a header if we've reached this point.
    if (header_->data_length == payload_.size()) {
        auto packet = std::make_unique<apacket>();
        packet->msg = *header_;
        packet->payload = std::move(payload_);
        read_callback_(this, std::move(packet));

        header_.reset();
        payload_.clear();
    }

    return true;
}

BlockingConnectionAdapter::BlockingConnectionAdapter(std::unique_ptr<BlockingConnection> connection)
    : underlying_(std::move(connection)) {}

BlockingConnectionAdapter::~BlockingConnectionAdapter() {
    LOG(INFO) << "BlockingConnectionAdapter(" << this->transport_name_ << "): destructing";
    CHECK(stopped_);
    read_thread_.join();
    write_thread_.join();
}

void BlockingConnectionAdapter::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (started_) {
        LOG(FATAL) << "BlockingConnectionAdapter(" << this->transport_name_
                   << "): started multiple times";
    }

    read_thread_ = std::thread([this]() {
        LOG(INFO) << this->transport_name_ << ": read thread spawning";
        while (true) {
            auto packet = std::make_unique<apacket>();
            if (!underlying_->Read(packet.get())) {
                PLOG(INFO) << this->transport_name_ << ": read failed";
                break;
            }
            read_callback_(this, std::move(packet));
        }
        HandleError("read failed");
    });

    write_thread_ = std::thread([this]() {
        LOG(INFO) << this->transport_name_ << ": write thread spawning";
        while (true) {
            std::unique_lock<std::mutex> lock(mutex_);
            ScopedAssumeLocked assume_locked(mutex_);
            cv_.wait(lock, [this]() REQUIRES(mutex_) {
                return this->stopped_ || !this->write_queue_.empty();
            });

            if (this->stopped_) {
                return;
            }

            std::unique_ptr<apacket> packet = std::move(this->write_queue_.front());
            this->write_queue_.pop_front();
            lock.unlock();

            if (!this->underlying_->Write(packet.get())) {
                break;
            }
        }
        HandleError("write failed");
    });

    started_ = true;
}

void BlockingConnectionAdapter::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_) {
            LOG(INFO) << "BlockingConnectionAdapter(" << this->transport_name_ << "): not started";
            return;
        }

        if (stopped_) {
            LOG(INFO) << "BlockingConnectionAdapter(" << this->transport_name_
                      << "): already stopped";
            return;
        }

        stopped_ = true;
    }

    LOG(INFO) << "BlockingConnectionAdapter(" << this->transport_name_ << "): stopping";

    this->underlying_->Close();
    this->cv_.notify_one();
}

bool BlockingConnectionAdapter::Write(std::unique_ptr<apacket> packet) {
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        write_queue_.emplace_back(std::move(packet));
    }

    cv_.notify_one();
    return true;
}

bool FdConnection::Read(apacket* packet) {
    if (!ReadFdExactly(fd_.get(), &packet->msg, sizeof(amessage))) {
        D("remote local: read terminated (message)");
        return false;
    }

    if (packet->msg.data_length > MAX_PAYLOAD) {
        D("remote local: read overflow (data length = %" PRIu32 ")", packet->msg.data_length);
        return false;
    }

    Block data(packet->msg.data_length);
    if (!ReadFdExactly(fd_.get(), data.data(), data.size())) {
        D("remote local: terminated (data)");
        return false;
    }
    packet->payload = std::move(data);

    return true;
}

bool FdConnection::Write(apacket* packet) {
    if (!WriteFdExactly(fd_.get(), &packet->msg, sizeof(packet->msg))) {
        D("remote local: write terminated");
        return false;
    }

    if (packet->msg.data_length) {
        return packet->payload.iterate_blocks([&](const char* data, size_t len) {
            if (!WriteFdExactly(fd_.get(), data, len)) {
                D("remote local: write terminated");
                return false;
            }
            return true;
        });
    }

    return true;
}

void FdConnection::Close() {
    adb_shutdown(fd_.get());
    fd_.reset();
}
