#include "tcp_imp_socket_impl.hh"

//! Specializations of TCPImpSocket for TCPOverIPv4OverTunFdAdapter and its lossy version
template class TCPImpSocket<TCPOverIPv4OverTunFdAdapter>;
template class TCPImpSocket<LossyFdAdapter<TCPOverIPv4OverTunFdAdapter>>;
