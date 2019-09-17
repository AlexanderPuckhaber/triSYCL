#ifndef TRISYCL_SYCL_VENDOR_XILINX_ACAP_AIE_TILE_BASE_HPP
#define TRISYCL_SYCL_VENDOR_XILINX_ACAP_AIE_TILE_BASE_HPP

/** \file

    The basic AI Engine homogeneous tile, with the common
    infrastructure to all the tiles (i.e. independent of x & y
    coordinates)

    Ronan dot Keryell at Xilinx dot com

    This file is distributed under the University of Illinois Open Source
    License. See LICENSE.TXT for details.
*/

#include <thread>

#include <boost/format.hpp>

#include "axi_stream_switch.hpp"

namespace trisycl::vendor::xilinx::acap::aie {

/// \ingroup aie
/// @{

/** The AI Engine tile infrastructure common to all the tiles

    This allows some type erasure while accessing the common
    tile infrastructure.

    \param AIE_Program is the type representing the full CGRA with the
    programs and memory contents
*/
template <typename AIE_Program>
class tile_base {
  /// For the aie::program class to play with our thread
  friend AIE_Program;

  using axi_ss_geo = typename AIE_Program::geo::core_axi_stream_switch;
  using mpl = typename axi_ss_geo::master_port_layout;
  using spl = typename axi_ss_geo::slave_port_layout;
  using axi_ss_t = axi_stream_switch<axi_ss_geo>;

  /// The AXI stream switch of the core tile
  axi_ss_t axi_ss;

  /// The thread used to run this tile
  std::thread thread;

protected:

  /// Keep a reference to the program owning this tile
  AIE_Program *aie_program;

private:

  /** Map the user input port number to the AXI stream switch port

      \param[in] port is the user port to use
  */
  static auto translate_input_port(int port) {
    return axi_ss_t::translate_port(port, mpl::me_0, mpl::me_last,
                                    "The core input port is out of range");
  }


  /** Map the user output port number to the AXI stream switch port

      \param[in] port is the BLI id/port to use
  */
  static auto translate_output_port(int port) {
    return axi_ss_t::translate_port(port, spl::me_0, spl::me_last,
                                    "The core output port is out of range");
  }

public:

  /** Provide a run member function that does nothing so it is
      possible to write a minimum AI Engine program that does nothing.

      Note that even if this function is not virtual, in the common
      case a programmer implements it to specify the program executed
      by a tile
  */
  void run() {
  }


  /** Get the user input connection from the AXI stream switch

      \param[in] port is the port to use
  */
  auto& in_connection(int port) {
    return axi_ss.in_connection(translate_input_port(port));
  }


  /** Get the user output connection to the AXI stream switch

      \param[in] port is port to use
  */
  auto& out_connection(int port) {
    return axi_ss.out_connection(translate_output_port(port));
  }


  /** Get the user input port from the AXI stream switch

      \param[in] T is the data type to be used in the transfers

      \param[in] Target specifies if the connection is blocking or
      not. It is blocking by default

      \param[in] port is the port to use
  */
  template <typename T, access::target Target = access::target::blocking_pipe>
  auto in(int port) {
    return axi_ss.in_connection(translate_input_port(port))
      .template in<T, Target>();
  }


  /** Get the user output port to the AXI stream switch

      \param[in] T is the data type to be used in the transfers

      \param[in] Target specifies if the connection is blocking or
      not. It is blocking by default

      \param[in] port is the port to use
  */
  template <typename T, access::target Target = access::target::blocking_pipe>
  auto out(int port) {
    return axi_ss.out_connection(translate_output_port(port))
      .template out<T, Target>();
  }


  /// Store a way to access to the owner CGRA
  void set_program(AIE_Program *program) {
    aie_program = program;
  }

};

/// @} End the aie Doxygen group

}

/*
    # Some Emacs stuff:
    ### Local Variables:
    ### ispell-local-dictionary: "american"
    ### eval: (flyspell-prog-mode)
    ### End:
*/

#endif // TRISYCL_SYCL_VENDOR_XILINX_ACAP_AIE_TILE_BASE_HPP
