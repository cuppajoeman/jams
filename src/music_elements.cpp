// std::size_t time_point_hash::operator()(
//     const std::chrono::steady_clock::time_point &tp) const {
//   return std::hash<std::int64_t>()(
//       std::chrono::duration_cast<std::chrono::nanoseconds>(
//           tp.time_since_epoch())
//           .count());
// }
