#ifndef TRISYCL_SYCL_VENDOR_XILINX_ACAP_AIE_RPC_HPP
#define TRISYCL_SYCL_VENDOR_XILINX_ACAP_AIE_RPC_HPP

/** \file

    This file implement the RPC system used for device -> host communication.

    Ronan dot Keryell at Xilinx dot com

    This file is distributed under the University of Illinois Open Source
    License. See LICENSE.TXT for details.
 */

#ifdef __SYCL_XILINX_AIE__
#include "hardware.hpp"
#include "lock.hpp"
#ifndef __SYCL_DEVICE_ONLY__
#include "xaie_wrapper.hpp"
#endif
#endif

#include <cstdint>
#include <functional>
#include <map>
#include <typeindex>
#include <variant>
#include <string>
#include <iostream>

#include "triSYCL/detail/overloaded.hpp"

namespace trisycl::vendor::xilinx::acap::aie {

/// This is a convenience wrapper that allows creating functor based RPC easily.
template<typename DataTy>
struct functor_rpc {
  using data_type = DataTy;

#if !defined(__SYCL_DEVICE_ONLY__) && defined(__SYCL_XILINX_AIE__)
  std::function<uint32_t(int, int, xaie::handle, data_type)> impl{};

  /// This is only one instance of this per template parameter, this function
  /// will get that instance.
  static functor_rpc &get() {
    static functor_rpc val;
    return val;
  }

  template<typename Func>
  static void set_handler(Func&& func) {
    get().impl = std::forward<Func>(func);
  }

  /// Process data that is received.
  /// This is executed each time new data arrives.
  static uint32_t act_on_data(int x, int y, xaie::handle h, data_type d) {
    return get().impl(x, y, h, d);
  }
#endif
};

/// This is the data that will be transmitted from the device to the host to
/// update the image. This struct needs to have the same layout on the host and
/// the device.
struct image_update_data {
  /// a pointer the new image data.
  hw::stable_pointer<void> data;
  /// the minimun value of a pixel.
  uint64_t min_value;
  /// the maximum value of a pixel.
  uint64_t max_value;
  /// a count of the number of image_update_data that was sent.
  uint32_t counter;
};

/// The act_on_data has dependencies on graphics.hpp but we do not want to
/// include graphics.hpp here so we use functor_rpc to have a type-erased RPC
/// class. and the functor is setup in graphics.hpp.
using image_update_rpc = functor_rpc<image_update_data>;

struct send_log_rpc {
  /// This is the data that will be transmitted when to the host when the device
  /// is logging This struct needs to have the same layout on the host and the
  /// device.
  struct data_type {
    /// a pointer to a buffer the first character to print
    hw::stable_pointer<const char> data;
    /// the number of characters to print
    uint64_t size;
  };
#if !defined(__SYCL_DEVICE_ONLY__) && defined(__SYCL_XILINX_AIE__)
  static uint32_t act_on_data(int x, int y, xaie::handle h,
                              data_type dev_data) {
    /// Decompose the device pointer into direction of the tile and address in the tile.
    hw::dev_ptr data_ptr = hw::get_dev_ptr({x, y}, dev_data.data);
    std::string str;
    str.resize(dev_data.size);
    /// Copy the indicated device data into a string.
    h.moved(data_ptr.p).memcpy_d2h(str.data(), data_ptr.offset, str.size());
    std::cout << str << std::flush;
    return 0;
  }
#endif
};

/// done_rpc is handled in a special wait because indicate if a kernel is done executing.
struct done_rpc {
  /// it needs no data, since it is just a signal.
  struct data_type {};
#if !defined(__SYCL_DEVICE_ONLY__) && defined(__SYCL_XILINX_AIE__)
  static uint32_t act_on_data(int x, int y, xaie::handle h, data_type d) {
    /// The effect of the signal are handled directly by wait_all
    assert(false && "This should be handeled by wait_all");
    return 0;
  }
#endif
};

template <typename... Tys> struct rpc_impl {
  using Var = std::variant<typename Tys::data_type...>;
#if !defined(__SYCL_DEVICE_ONLY__) && defined(__SYCL_XILINX_AIE__)
  struct host_side {
    int x_size, y_size;
    xaie::handle h;
    uint32_t addr;

    /// this will retrun a handle to the synchronization barrier between the device and the host.
    soft_barrier::host_side get_barrier(int x, int y) {
      return {h.moved(x, y), (uint32_t)(addr + offsetof(device_side, barrier))};
    }

    /// This will invoke the correct function to process the data in v
    uint32_t visit(int x, int y, xaie::handle h, Var v) {
      auto visitor = detail::overloaded{[&](typename Tys::data_type data) {
        return Tys::act_on_data(x, y, h, data);
      }...};
      return std::visit(visitor, v);
    }

    /// This will wait on every kernel while handling there RPC requests
    void wait_all() {
      ::trisycl::detail::no_log_in_this_scope nls;
      /// This count the number of kernel that indicated they finished
      /// executing. any kernel can signal it finished executing just once
      /// because it stop executing or get stuck in an infinite loop after that.
      /// so it is not needed to keep track of which kernel stoped executing
      /// just how many.
      int done_counter = 0;
      do {
        for (int x = 0; x < x_size; x++)
          for (int y = 0; y < y_size; y++) {
            /// If try_arrive returns true the device has written data and is
            /// wating on the host to act on it
            if (!get_barrier(x, y).try_arrive())
              continue;
            Var data;
            /// Read the data the device has written.
            h.moved(x, y).memcpy_d2h(&data, addr + offsetof(device_side, data),
                                     sizeof(Var));
            /// This deals with the special case of a kernel indicating it is
            /// done. This kernel stoped executing.
            if (data.index() == 0) {
              done_counter++;
            } else {
              /// Otherwise call the appropriate function.
              auto ret = visit(x, y, h.moved(x, y), data);
              /// And write back the response.
              h.moved(x, y).mem_write(addr + offsetof(device_side, ret_val),
                                      ret);
            }
            get_barrier(x, y).wait();
          }
        /// Wait until all kernel have finished.
      } while (done_counter < x_size * y_size);
    }
  };
#endif

  struct device_side {
#if defined(__SYCL_XILINX_AIE__)
#ifdef __SYCL_DEVICE_ONLY__
    static device_side *get() {
      return (device_side *)(acap::hw::self_tile_addr(
                                     acap::hw::get_parity_dev()) +
                                 acap::hw::rpc_record_begin_offset);
    }
#endif
    soft_barrier::device_side barrier;
    Var data;
    uint32_t ret_val;

    /// This sent data to the host to be processed.
    template <typename Ty> uint32_t perform(Ty &&d) {
      /// Write the data.
      data = d;
      /// Notify the host of the data being available.
      barrier.wait();
      /// Wait for the host to process the data.
      barrier.wait();
      return ret_val;
    }
#endif
  };
};

using rpc = rpc_impl<done_rpc, image_update_rpc, send_log_rpc>;

#if defined(__SYCL_XILINX_AIE__)
// The advantage of this over static_assert is that V1 and V2 will be printed when it fails.
template<auto V1, auto V2>
struct assert_equal {
  static_assert(V1 == V2, "");
};

/// This variable is just to check the rpc size
constexpr assert_equal<sizeof(rpc::device_side), hw::rpc_record_size> v;
#endif

} // namespace trisycl::vendor::xilinx::acap::aie

#endif
