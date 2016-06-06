/*
 * Copyright 2016 ScyllaDB
 **/

/* This file is part of Scylla.
 *
 * Scylla is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Scylla is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Scylla.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <boost/signals2.hpp>
#include <boost/signals2/dummy_mutex.hpp>
#include <type_traits>

#include "utils/exceptions.hh"
#include <core/future.hh>

namespace bs2 = boost::signals2;

using disk_error_signal_type = bs2::signal_type<void (), bs2::keywords::mutex_type<bs2::dummy_mutex>>::type;

extern thread_local disk_error_signal_type commit_error;
extern thread_local disk_error_signal_type sstable_read_error;
extern thread_local disk_error_signal_type sstable_write_error;
extern thread_local disk_error_signal_type general_disk_error;

bool should_stop_on_system_error(const std::system_error& e);

template<typename Func, typename... Args>
std::enable_if_t<!is_future<std::result_of_t<Func(Args&&...)>>::value,
		         std::result_of_t<Func(Args&&...)>>
do_io_check(disk_error_signal_type& signal, Func&& func, Args&&... args) {
    try {
        // calling function
        return func(std::forward<Args>(args)...);
    } catch (std::system_error& e) {
        if (should_stop_on_system_error(e)) {
            signal();
            throw storage_io_error(e);
        }
        throw;
    }
}

template<typename Func, typename... Args,
         typename RetType = typename std::enable_if<is_future<std::result_of_t<Func(Args&&...)>>::value>::type>
auto do_io_check(disk_error_signal_type& signal, Func&& func, Args&&... args) {
    try {
        // calling function
        auto fut = func(std::forward<Args>(args)...);
        return fut.handle_exception([&signal] (auto ep) {
            try {
                std::rethrow_exception(ep);
            } catch (std::system_error& sys_err) {
                if (should_stop_on_system_error(sys_err)) {
                    signal();
                    throw storage_io_error(sys_err);
                }
            }
            return futurize<std::result_of_t<Func(Args&&...)>>::make_exception_future(ep);
        });
    } catch (std::system_error& e) {
        if (should_stop_on_system_error(e)) {
            signal();
            throw storage_io_error(e);
        }
        throw;
    }
}

template<typename Func, typename... Args>
auto commit_io_check(Func&& func, Args&&... args) {
    return do_io_check(commit_error, func, std::forward<Args>(args)...);
}

template<typename Func, typename... Args>
auto sstable_read_io_check(Func&& func, Args&&... args) {
    return do_io_check(sstable_read_error, func, std::forward<Args>(args)...);
}

template<typename Func, typename... Args>
auto sstable_write_io_check(Func&& func, Args&&... args) {
    return do_io_check(sstable_write_error, func, std::forward<Args>(args)...);
}

template<typename Func, typename... Args>
auto io_check(Func&& func, Args&&... args) {
    return do_io_check(general_disk_error, func, std::forward<Args>(args)...);
}
