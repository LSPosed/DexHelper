#include "slicer/dex_format.h"
#include "slicer/dex_leb128.h"
#include "slicer/reader.h"

#include <algorithm>

#include "dex_helper.h"

DexHelper::DexHelper(
    const std::vector<std::tuple<const void *, size_t>> &dexs) {
  for (const auto &[image, size] : dexs) {
    readers_.emplace_back(static_cast<const dex::u1 *>(image), size);
  }
  size_t dex_count = readers_.size();

  // init
  rev_method_indices_.resize(dex_count);
  rev_class_indices_.resize(dex_count);
  rev_field_indices_.resize(dex_count);
  strings_.resize(dex_count);
  method_codes_.resize(dex_count);
  method_params_.resize(dex_count);
  string_cache_.resize(dex_count);
  type_cache_.resize(dex_count);
  field_cache_.resize(dex_count);
  method_cache_.resize(dex_count);
  class_cache_.resize(dex_count);
  invoking_cache_.resize(dex_count);
  invoked_cache_.resize(dex_count);
  getting_cache_.resize(dex_count);
  setting_cache_.resize(dex_count);
  declaring_cache_.resize(dex_count);
  searched_methods_.resize(dex_count);

  for (size_t dex_idx = 0; dex_idx < dex_count; ++dex_idx) {
    auto &dex = readers_[dex_idx];
    rev_method_indices_[dex_idx].resize(dex.MethodIds().size(), size_t(-1));
    rev_class_indices_[dex_idx].resize(dex.TypeIds().size(), size_t(-1));
    rev_field_indices_[dex_idx].resize(dex.FieldIds().size(), size_t(-1));

    strings_[dex_idx].reserve(dex.StringIds().size());
    method_codes_[dex_idx].resize(dex.MethodIds().size(), nullptr);
    method_params_[dex_idx].resize(dex.MethodIds().size(), nullptr);

    type_cache_[dex_idx].resize(dex.StringIds().size(), dex::kNoIndex);
    field_cache_[dex_idx].resize(dex.TypeIds().size());
    method_cache_[dex_idx].resize(dex.TypeIds().size());
    class_cache_[dex_idx].resize(dex.TypeIds().size(), dex::kNoIndex);

    string_cache_[dex_idx].resize(dex.StringIds().size());
    invoking_cache_[dex_idx].resize(dex.MethodIds().size());
    invoked_cache_[dex_idx].resize(dex.MethodIds().size());
    getting_cache_[dex_idx].resize(dex.FieldIds().size());
    setting_cache_[dex_idx].resize(dex.FieldIds().size());
    declaring_cache_[dex_idx].resize(dex.TypeIds().size());

    searched_methods_[dex_idx].resize(dex.MethodIds().size());
  }

  for (size_t dex_idx = 0; dex_idx < dex_count; ++dex_idx) {
    auto &dex = readers_[dex_idx];
    auto &strs = strings_[dex_idx];
    for (const auto &str : dex.StringIds()) {
      const dex::u1 *ptr =
          reinterpret_cast<const dex::u1 *>(dex.Image() + str.string_data_off);
      size_t len = dex::ReadULeb128(&ptr);
      strs.emplace_back(reinterpret_cast<const char *>(ptr), len);
    }
  }

  for (size_t dex_idx = 0; dex_idx < dex_count; ++dex_idx) {
    auto &dex = readers_[dex_idx];
    for (size_t class_idx = 0; class_idx < dex.ClassDefs().size();
         ++class_idx) {
      const auto &class_def = dex.ClassDefs()[class_idx];
      class_cache_[dex_idx][class_def.class_idx] = class_idx;
      if (class_def.class_data_off == 0)
        continue;
      const auto *class_data = reinterpret_cast<const dex::u1 *>(
          dex.Image() + class_def.class_data_off);
      dex::u4 static_fields_count = dex::ReadULeb128(&class_data);
      dex::u4 instance_fields_count = dex::ReadULeb128(&class_data);
      dex::u4 direct_methods_count = dex::ReadULeb128(&class_data);
      dex::u4 virtual_methods_count = dex::ReadULeb128(&class_data);

      auto &codes = method_codes_[dex_idx];
      auto &params = method_params_[dex_idx];
      codes.resize(dex.MethodIds().size(), nullptr);

      for (dex::u4 i = 0; i < static_fields_count; ++i) {
        dex::ReadULeb128(&class_data);
        dex::ReadULeb128(&class_data);
      }

      for (dex::u4 i = 0; i < instance_fields_count; ++i) {
        dex::ReadULeb128(&class_data);
        dex::ReadULeb128(&class_data);
      }

      for (dex::u4 i = 0, method_idx = 0; i < direct_methods_count; ++i) {
        method_idx += dex::ReadULeb128(&class_data);

        auto access_flags = dex::ReadULeb128(&class_data);
        auto offset = dex::ReadULeb128(&class_data);
        if (offset != 0) {
          codes[method_idx] =
              reinterpret_cast<const dex::Code *>(dex.Image() + offset);
        }
        auto parameters_offset =
            dex.ProtoIds()[dex.MethodIds()[method_idx].proto_idx]
                .parameters_off;
        if (parameters_offset) {
          params[method_idx] = reinterpret_cast<const dex::TypeList *>(
              dex.Image() + parameters_offset);
        }
      }

      for (dex::u4 i = 0, method_idx = 0; i < virtual_methods_count; ++i) {
        method_idx += dex::ReadULeb128(&class_data);

        auto access_flags = dex::ReadULeb128(&class_data);
        auto offset = dex::ReadULeb128(&class_data);
        if (offset != 0) {
          codes[method_idx] =
              reinterpret_cast<const dex::Code *>(dex.Image() + offset);
        }
        auto parameters_offset =
            dex.ProtoIds()[dex.MethodIds()[method_idx].proto_idx]
                .parameters_off;
        if (parameters_offset) {
          params[method_idx] = reinterpret_cast<const dex::TypeList *>(
              dex.Image() + parameters_offset);
        }
      }
    }
  }
  for (size_t dex_idx = 0; dex_idx < dex_count; ++dex_idx) {
    auto &dex = readers_[dex_idx];
    auto &type = type_cache_[dex_idx];
    auto &field = field_cache_[dex_idx];
    auto &declare = declaring_cache_[dex_idx];
    auto &method = method_cache_[dex_idx];
    for (size_t type_idx = 0; type_idx < dex.TypeIds().size(); ++type_idx) {
      type[dex.TypeIds()[type_idx].descriptor_idx] = type_idx;
    }
    for (size_t field_idx = 0; field_idx < dex.FieldIds().size(); ++field_idx) {
      auto f = dex.FieldIds()[field_idx];
      field[f.class_idx][f.name_idx] = field_idx;
      declare[f.type_idx].emplace_back(field_idx);
    }
    for (size_t method_idx = 0; method_idx < dex.MethodIds().size();
         ++method_idx) {
      auto m = dex.MethodIds()[method_idx];
      method[m.class_idx][m.name_idx].emplace_back(method_idx);
    }
  }
}

std::tuple<uint32_t, uint32_t>
DexHelper::FindPrefixStringId(size_t dex_idx, std::string_view to_find) const {
  auto &strs = strings_[dex_idx];
  if (auto str_lower_bound =
          std::lower_bound(strs.cbegin(), strs.cend(), to_find),
      str_upper_bound = std::upper_bound(strs.cbegin(), strs.cend(),
                                         std::string(to_find) + '\xff');
      str_upper_bound != strs.cend() && str_lower_bound != strs.cend() &&
      str_lower_bound <= str_upper_bound) {
    return {str_lower_bound - strs.cbegin(), str_upper_bound - strs.cbegin()};
  } else {
    return {dex::kNoIndex, dex::kNoIndex};
  }
}

uint32_t DexHelper::FindPrefixStringIdExact(size_t dex_idx,
                                            std::string_view to_find) const {
  auto &strs = strings_[dex_idx];
  auto first = std::lower_bound(strs.cbegin(), strs.cend(), to_find);
  if (first != strs.cend() && *first == to_find)
    return first - strs.cbegin();
  return dex::kNoIndex;
}

void DexHelper::CreateFullCache() const {
  for (size_t dex_idx = 0; dex_idx < readers_.size(); ++dex_idx) {
    auto &codes = method_codes_[dex_idx];
    for (size_t method_id = 0; method_id < codes.size(); ++method_id) {
      ScanMethod(dex_idx, method_id);
    }
  }
}

bool DexHelper::ScanMethod(size_t dex_idx, uint32_t method_id, size_t str_lower,
                           size_t str_upper) const {
  auto &str_cache = string_cache_[dex_idx];
  auto &inv_cache = invoking_cache_[dex_idx];
  auto &inved_cache = invoked_cache_[dex_idx];
  auto &get_cache = getting_cache_[dex_idx];
  auto &set_cache = setting_cache_[dex_idx];
  auto &scanned = searched_methods_[dex_idx];

  bool match_str = false;
  if (scanned[method_id])
    return match_str;
  scanned[method_id] = true;
  auto &code = method_codes_[dex_idx][method_id];
  if (!code)
    return match_str;
  const dex::u2 *inst = code->insns;
  const dex::u2 *end = code->insns + code->insns_size;
  size_t ins_count = 0;
  while (inst < end) {
    ins_count++;
    dex::u1 opcode = *inst & 0xff;
    if (opcode == 0x1a) {
      auto str_idx = inst[1];
      if (str_lower <= str_idx && str_upper > str_idx) {
        match_str = true;
      }
      str_cache[str_idx].emplace_back(method_id);
    }
    if (opcode == 0x1b) {
      auto str_idx = *reinterpret_cast<const dex::u4 *>(&inst[1]);
      if (str_lower <= str_idx && str_upper > str_idx) {
        match_str = true;
      }
      str_cache[str_idx].emplace_back(method_id);
    }
    if ((opcode >= 0x52 && opcode <= 0x58) ||
        (opcode >= 0x60 && opcode <= 0x66)) {
      auto field_idx = inst[1];
      get_cache[field_idx].emplace_back(method_id);
    }
    if ((opcode >= 0x59 && opcode <= 0x5f) ||
        (opcode >= 0x67 && opcode <= 0x6d)) {
      auto field_idx = inst[1];
      set_cache[field_idx].emplace_back(method_id);
    }
    if ((opcode >= 0x74 && opcode <= 0x78) ||
        (opcode >= 0x6e && opcode <= 0x72)) {
      auto callee = inst[1];
      inv_cache[method_id].emplace_back(callee);
      inved_cache[callee].emplace_back(method_id);
    }
    if (opcode == 0x00) {
      if (*inst == 0x0100) {
        // packed-switch-payload
        inst += inst[1] * 2 + 3;
      } else if (*inst == 0x0200) {
        // sparse-switch-payload
        inst += inst[1] * 4 + 1;
      } else if (*inst == 0x0300) {
        // fill-array-data-payload
        inst +=
            (*reinterpret_cast<const dex::u4 *>(&inst[2]) * inst[1] + 1) / 2 +
            3;
      }
    }
    inst += opcode_len[opcode];
  }
  return match_str;
}

std::tuple<std::vector<std::vector<uint32_t>>,
           std::vector<std::vector<uint32_t>>>
DexHelper::ConvertParameters(
    const std::vector<size_t> &parameter_types,
    const std::vector<size_t> &contains_parameter_types) const {
  std::vector<std::vector<uint32_t>> parameter_types_ids(readers_.size());
  std::vector<std::vector<uint32_t>> contains_parameter_types_ids(
      readers_.size());
  if (!parameter_types.empty()) {
    for (size_t dex_idx = 0; dex_idx < readers_.size(); ++dex_idx) {
      parameter_types_ids[dex_idx].reserve(parameter_types.size());
    }
    for (auto &param : parameter_types) {
      if (param != size_t(-1) && param >= class_indices_.size())
        return {parameter_types_ids, contains_parameter_types_ids};
      auto &ids = class_indices_[param];
      for (size_t dex_idx = 0; dex_idx < readers_.size(); ++dex_idx) {
        parameter_types_ids[dex_idx].emplace_back(ids[dex_idx]);
      }
    }
  }

  if (!contains_parameter_types.empty()) {
    for (size_t dex_idx = 0; dex_idx < readers_.size(); ++dex_idx) {
      contains_parameter_types_ids[dex_idx].reserve(
          contains_parameter_types.size());
    }
    for (auto &param : contains_parameter_types) {
      if (param != size_t(-1) && param >= class_indices_.size())
        return {parameter_types_ids, contains_parameter_types_ids};
      auto &ids = class_indices_[param];
      for (size_t dex_idx = 0; dex_idx < readers_.size(); ++dex_idx) {
        contains_parameter_types_ids[dex_idx].emplace_back(ids[dex_idx]);
      }
    }
  }
  return {parameter_types_ids, contains_parameter_types_ids};
}

std::vector<size_t> DexHelper::FindMethodUsingString(
    std::string_view str, bool match_prefix, size_t return_type,
    short parameter_count, std::string_view parameter_shorty,
    size_t declaring_class, const std::vector<size_t> &parameter_types,
    const std::vector<size_t> &contains_parameter_types,
    const std::vector<size_t> &dex_priority, bool find_first) const {

  std::vector<size_t> out;

  if (return_type != size_t(-1) && return_type >= class_indices_.size())
    return out;
  if (declaring_class != size_t(-1) && declaring_class >= class_indices_.size())
    return out;
  const auto [parameter_types_ids, contains_parameter_types_ids] =
      ConvertParameters(parameter_types, contains_parameter_types);

  for (auto dex_idx : GetPriority(dex_priority)) {
    uint32_t lower, upper;
    if (match_prefix) {
      std::tie(lower, upper) = FindPrefixStringId(dex_idx, str);
      if (lower == dex::kNoIndex)
        continue;
    } else {
      lower = upper = FindPrefixStringIdExact(dex_idx, str);
      if (lower == dex::kNoIndex)
        continue;
      ++upper;
    }
    auto &codes = method_codes_[dex_idx];
    auto &strs = string_cache_[dex_idx];

    if (find_first) {
      for (auto s = lower; s < upper; ++s) {
        for (auto &m : strs[s]) {
          out.emplace_back(CreateMethodIndex(dex_idx, m));
          return out;
        }
      }
    }

    for (size_t method_id = 0; method_id < codes.size(); ++method_id) {
      auto &scanned = searched_methods_[dex_idx];
      if (scanned[method_id])
        continue;
      if (!IsMethodMatch(dex_idx, method_id,
                         return_type == size_t(-1)
                             ? dex::kNoIndex
                             : class_indices_[return_type][dex_idx],
                         parameter_count, parameter_shorty,
                         declaring_class == size_t(-1)
                             ? dex::kNoIndex
                             : class_indices_[declaring_class][dex_idx],
                         parameter_types_ids[dex_idx],
                         contains_parameter_types_ids[dex_idx])) {
        continue;
      }
      bool match = ScanMethod(dex_idx, method_id, lower, upper);
      if (match && find_first)
        break;
    }

    for (auto s = lower; s < upper; ++s) {
      for (auto &m : strs[s]) {
        out.emplace_back(CreateMethodIndex(dex_idx, m));
        if (find_first)
          return out;
      }
    }
  }
  return out;
}

std::vector<size_t> DexHelper::FindMethodInvoking(
    size_t method_idx, size_t return_type, short parameter_count,
    std::string_view parameter_shorty, size_t declaring_class,
    const std::vector<size_t> &parameter_types,
    const std::vector<size_t> &contains_parameter_types,
    const std::vector<size_t> &dex_priority, bool find_first) const {

  std::vector<size_t> out;

  if (method_idx >= method_indices_.size())
    return out;
  if (return_type != size_t(-1) && return_type >= class_indices_.size())
    return out;
  if (declaring_class != size_t(-1) && declaring_class >= class_indices_.size())
    return out;
  const auto [parameter_types_ids, contains_parameter_types_ids] =
      ConvertParameters(parameter_types, contains_parameter_types);

  const auto method_ids = method_indices_[method_idx];

  for (auto dex_idx : GetPriority(dex_priority)) {
    auto &codes = method_codes_[dex_idx];
    auto caller_id = method_ids[dex_idx];
    if (caller_id == dex::kNoIndex)
      continue;
    ScanMethod(dex_idx, caller_id);
    for (auto callee_id : invoking_cache_[dex_idx][caller_id]) {
      if (!IsMethodMatch(dex_idx, callee_id,
                         return_type == size_t(-1)
                             ? dex::kNoIndex
                             : class_indices_[return_type][dex_idx],
                         parameter_count, parameter_shorty,
                         declaring_class == size_t(-1)
                             ? dex::kNoIndex
                             : class_indices_[declaring_class][dex_idx],
                         parameter_types_ids[dex_idx],
                         contains_parameter_types_ids[dex_idx])) {
        continue;
      }
      out.emplace_back(CreateMethodIndex(dex_idx, callee_id));
      if (find_first)
        return out;
    }
  }
  return out;
}

std::vector<size_t> DexHelper::FindMethodInvoked(
    size_t method_idx, size_t return_type, short parameter_count,
    std::string_view parameter_shorty, size_t declaring_class,
    const std::vector<size_t> &parameter_types,
    const std::vector<size_t> &contains_parameter_types,
    const std::vector<size_t> &dex_priority, bool find_first) const {

  std::vector<size_t> out;

  if (method_idx >= method_indices_.size())
    return out;
  if (return_type != size_t(-1) && return_type >= class_indices_.size())
    return out;
  if (declaring_class != size_t(-1) && declaring_class >= class_indices_.size())
    return out;
  const auto [parameter_types_ids, contains_parameter_types_ids] =
      ConvertParameters(parameter_types, contains_parameter_types);

  const auto method_ids = method_indices_[method_idx];

  for (auto dex_idx : GetPriority(dex_priority)) {
    auto callee_id = method_ids[dex_idx];
    if (callee_id == dex::kNoIndex)
      continue;
    auto &codes = method_codes_[dex_idx];
    auto &cache = invoked_cache_[dex_idx][callee_id];
    if (find_first && !cache.empty()) {
      out.emplace_back(CreateMethodIndex(dex_idx, cache.front()));
      return out;
    }
    for (size_t method_id = 0; method_id < codes.size(); ++method_id) {
      auto &scanned = searched_methods_[dex_idx];
      if (scanned[method_id])
        continue;
      if (!IsMethodMatch(dex_idx, method_id,
                         return_type == size_t(-1)
                             ? dex::kNoIndex
                             : class_indices_[return_type][dex_idx],
                         parameter_count, parameter_shorty,
                         declaring_class == size_t(-1)
                             ? dex::kNoIndex
                             : class_indices_[declaring_class][dex_idx],
                         parameter_types_ids[dex_idx],
                         contains_parameter_types_ids[dex_idx])) {
        continue;
      }
      ScanMethod(dex_idx, method_id);
      if (find_first && !cache.empty())
        break;
    }
    for (auto &caller : cache) {
      out.emplace_back(CreateMethodIndex(dex_idx, caller));
      if (find_first)
        return out;
    }
  }
  return out;
}

std::vector<size_t> DexHelper::FindMethodGettingField(
    size_t field_idx, size_t return_type, short parameter_count,
    std::string_view parameter_shorty, size_t declaring_class,
    const std::vector<size_t> &parameter_types,
    const std::vector<size_t> &contains_parameter_types,
    const std::vector<size_t> &dex_priority, bool find_first) const {
  std::vector<size_t> out;

  if (field_idx >= field_indices_.size())
    return out;
  if (return_type != size_t(-1) && return_type >= class_indices_.size())
    return out;
  if (declaring_class != size_t(-1) && declaring_class >= class_indices_.size())
    return out;
  const auto [parameter_types_ids, contains_parameter_types_ids] =
      ConvertParameters(parameter_types, contains_parameter_types);
  auto field_ids = field_indices_[field_idx];
  for (auto dex_idx : GetPriority(dex_priority)) {
    auto field_id = field_ids[dex_idx];
    if (field_id == dex::kNoIndex)
      continue;
    auto &codes = method_codes_[dex_idx];
    auto &cache = getting_cache_[dex_idx][field_id];
    if (find_first && !cache.empty()) {
      out.emplace_back(CreateMethodIndex(dex_idx, cache.front()));
      return out;
    }
    for (size_t method_id = 0; method_id < codes.size(); ++method_id) {
      auto &scanned = searched_methods_[dex_idx];
      if (scanned[method_id])
        continue;
      if (!IsMethodMatch(dex_idx, method_id,
                         return_type == size_t(-1)
                             ? dex::kNoIndex
                             : class_indices_[return_type][dex_idx],
                         parameter_count, parameter_shorty,
                         declaring_class == size_t(-1)
                             ? dex::kNoIndex
                             : class_indices_[declaring_class][dex_idx],
                         parameter_types_ids[dex_idx],
                         contains_parameter_types_ids[dex_idx])) {
        continue;
      }
      ScanMethod(dex_idx, method_id);
      if (find_first && !cache.empty())
        break;
    }
    for (auto &getter : cache) {
      out.emplace_back(CreateMethodIndex(dex_idx, getter));
      if (find_first)
        return out;
    }
  }
  return out;
}

std::vector<size_t> DexHelper::FindMethodSettingField(
    size_t field_idx, size_t return_type, short parameter_count,
    std::string_view parameter_shorty, size_t declaring_class,
    const std::vector<size_t> &parameter_types,
    const std::vector<size_t> &contains_parameter_types,
    const std::vector<size_t> &dex_priority, bool find_first) const {
  std::vector<size_t> out;

  if (field_idx >= field_indices_.size())
    return out;
  if (return_type != size_t(-1) && return_type >= class_indices_.size())
    return out;
  if (declaring_class != size_t(-1) && declaring_class >= class_indices_.size())
    return out;
  const auto [parameter_types_ids, contains_parameter_types_ids] =
      ConvertParameters(parameter_types, contains_parameter_types);
  auto field_ids = field_indices_[field_idx];
  for (auto dex_idx : GetPriority(dex_priority)) {
    auto field_id = field_ids[dex_idx];
    if (field_id == dex::kNoIndex)
      continue;
    auto &codes = method_codes_[dex_idx];
    auto &cache = setting_cache_[dex_idx][field_id];
    if (find_first && !cache.empty()) {
      out.emplace_back(CreateMethodIndex(dex_idx, cache.front()));
      return out;
    }
    for (size_t method_id = 0; method_id < codes.size(); ++method_id) {
      auto &scanned = searched_methods_[dex_idx];
      if (scanned[method_id])
        continue;
      if (!IsMethodMatch(dex_idx, method_id,
                         return_type == size_t(-1)
                             ? dex::kNoIndex
                             : class_indices_[return_type][dex_idx],
                         parameter_count, parameter_shorty,
                         declaring_class == size_t(-1)
                             ? dex::kNoIndex
                             : class_indices_[declaring_class][dex_idx],
                         parameter_types_ids[dex_idx],
                         contains_parameter_types_ids[dex_idx])) {
        continue;
      }
      ScanMethod(dex_idx, method_id);
      if (find_first && !cache.empty())
        break;
    }
    for (auto &getter : cache) {
      out.emplace_back(CreateMethodIndex(dex_idx, getter));
      if (find_first)
        return out;
    }
  }
  return out;
}
std::vector<size_t>
DexHelper::FindField(size_t type, const std::vector<size_t> &dex_priority,
                     bool find_first) const {
  std::vector<size_t> out;

  if (type >= class_indices_.size())
    return out;
  auto &type_ids = class_indices_[type];
  for (auto dex_idx : GetPriority(dex_priority)) {
    for (auto &field_id : declaring_cache_[dex_idx][type_ids[dex_idx]]) {
      out.emplace_back(CreateFieldIndex(dex_idx, field_id));
      if (find_first)
        return out;
    }
  }
  return out;
}

bool DexHelper::IsMethodMatch(
    size_t dex_id, uint32_t method_id, uint32_t return_type,
    short parameter_count, std::string_view parameter_shorty,
    uint32_t declaring_class, const std::vector<uint32_t> &parameter_types,
    const std::vector<uint32_t> &contains_parameter_types) const {
  auto &dex = readers_[dex_id];
  auto &method = dex.MethodIds()[method_id];
  auto &strs = strings_[dex_id];
  auto &params = method_params_[dex_id][method_id];
  size_t params_size = params ? params->size : 0;
  if (declaring_class != dex::kNoIndex && method.class_idx != declaring_class)
    return false;
  auto &proto = dex.ProtoIds()[method.proto_idx];
  auto &shorty = strs[proto.shorty_idx];
  if (return_type != dex::kNoIndex && proto.return_type_idx != return_type)
    return false;
  if (!parameter_shorty.empty() && shorty != parameter_shorty)
    return false;
  if (parameter_count != -1 && params_size != parameter_count)
    return false;
  if (!parameter_types.empty()) {
    if (parameter_types.size() != params_size)
      return false;
    for (size_t i = 0; i < params_size; ++i) {
      if (parameter_types[i] != params->list[i].type_idx)
        return false;
    }
  }
  if (!contains_parameter_types.empty()) {
    for (const auto &type : contains_parameter_types) {
      bool contains = false;
      for (size_t i = 0; i < params_size; ++i) {
        if (type == params->list[i].type_idx) {
          contains = true;
          break;
        }
      }
      if (!contains)
        return false;
    }
  }
  return true;
}
size_t DexHelper::CreateMethodIndex(
    std::string_view class_name, std::string_view method_name,
    const std::vector<std::string_view> &params_name, size_t on_dex) const {
  std::vector<uint32_t> method_ids;
  method_ids.resize(readers_.size(), dex::kNoIndex);
  for (size_t dex_idx = size_t(-1);
       dex_idx < readers_.size() || dex_idx == size_t(-1); ++dex_idx) {
    if (dex_idx == size_t(-1)) {
      dex_idx = on_dex;
    }
    if (dex_idx == size_t(-1)) {
      dex_idx = 0;
    }
    auto &strs = strings_[dex_idx];
    auto method_name_iter =
        std::lower_bound(strs.cbegin(), strs.cend(), method_name);
    if (method_name_iter == strs.cend() || *method_name_iter != method_name)
      continue;
    auto method_name_id = method_name_iter - strs.cbegin();
    auto class_name_iter =
        std::lower_bound(strs.cbegin(), strs.cend(), class_name);
    if (class_name_iter == strs.cend() || *class_name_iter != class_name)
      continue;
    auto class_name_id = class_name_iter - strs.cbegin();
    auto class_id = type_cache_[dex_idx][class_name_id];
    auto candidates = method_cache_[dex_idx][class_id].find(method_name_id);
    if (candidates == method_cache_[dex_idx][class_id].end())
      continue;
    for (auto &method_id : candidates->second) {
      auto &dex = readers_[dex_idx];
      auto params = method_params_[dex_idx][method_id];
      if (params && params->size != params_name.size())
        continue;
      if (!params_name.empty() && !params)
        continue;
      for (size_t i = 0; i < params_name.size(); ++i) {
        if (strs[dex.TypeIds()[params->list[i].type_idx].descriptor_idx] !=
            params_name[i])
          continue;
      }
      if (auto idx = rev_method_indices_[dex_idx][method_id]; idx != size_t(-1))
        return idx;
      method_ids[dex_idx] = method_id;
    }
  }
  auto index = method_indices_.size();
  for (size_t dex_id = 0; dex_id < readers_.size(); ++dex_id) {
    auto method_id = method_ids[dex_id];
    if (method_id != dex::kNoIndex)
      rev_method_indices_[dex_id][method_id] = index;
  }
  method_indices_.emplace_back(std::move(method_ids));
  return index;
}

size_t DexHelper::CreateClassIndex(std::string_view class_name,
                                   size_t on_dex) const {
  std::vector<uint32_t> class_ids;
  class_ids.resize(readers_.size(), dex::kNoIndex);
  for (size_t dex_idx = size_t(-1);
       dex_idx < readers_.size() || dex_idx == size_t(-1); ++dex_idx) {
    if (dex_idx == size_t(-1)) {
      dex_idx = on_dex;
    }
    if (dex_idx == size_t(-1)) {
      dex_idx = 0;
    }
    auto &strs = strings_[dex_idx];
    auto class_name_iter =
        std::lower_bound(strs.cbegin(), strs.cend(), class_name);
    if (class_name_iter == strs.cend() || *class_name_iter != class_name)
      continue;
    auto class_name_id = class_name_iter - strs.cbegin();
    auto class_id = type_cache_[dex_idx][class_name_id];
    if (auto idx = rev_class_indices_[dex_idx][class_id]; idx != size_t(-1))
      return idx;
    class_ids[dex_idx] = class_id;
  }

  auto index = class_indices_.size();
  for (size_t dex_id = 0; dex_id < readers_.size(); ++dex_id) {
    auto class_id = class_ids[dex_id];
    if (class_id != dex::kNoIndex)
      rev_class_indices_[dex_id][class_id] = index;
  }
  class_indices_.emplace_back(std::move(class_ids));
  return index;
}

size_t DexHelper::CreateFieldIndex(std::string_view class_name,
                                   std::string_view field_name,
                                   size_t on_dex) const {
  std::vector<uint32_t> field_ids;
  field_ids.resize(readers_.size(), dex::kNoIndex);

  for (size_t dex_idx = size_t(-1);
       dex_idx < readers_.size() || dex_idx == size_t(-1); ++dex_idx) {
    if (dex_idx == size_t(-1)) {
      dex_idx = on_dex;
    }
    if (dex_idx == size_t(-1)) {
      dex_idx = 0;
    }
    auto &strs = strings_[dex_idx];
    auto class_name_iter =
        std::lower_bound(strs.cbegin(), strs.cend(), class_name);
    if (class_name_iter == strs.cend() || *class_name_iter != class_name)
      continue;
    auto class_name_id = class_name_iter - strs.cbegin();
    auto field_name_iter =
        std::lower_bound(strs.cbegin(), strs.cend(), field_name);
    if (field_name_iter == strs.cend() || *field_name_iter != field_name)
      continue;
    auto field_name_id = field_name_iter - strs.cbegin();
    auto class_id = type_cache_[dex_idx][class_name_id];
    auto iter = field_cache_[dex_idx][class_id].find(field_name_id);
    if (iter == field_cache_[dex_idx][class_id].end())
      continue;
    auto field_id = iter->second;
    if (auto idx = rev_field_indices_[dex_idx][field_id]; idx != size_t(-1))
      return idx;
    field_ids[dex_idx] = field_id;
  }

  auto index = field_indices_.size();
  for (size_t dex_id = 0; dex_id < readers_.size(); ++dex_id) {
    auto field_id = field_ids[dex_id];
    if (field_id != dex::kNoIndex)
      rev_field_indices_[dex_id][field_id] = index;
  }
  field_indices_.emplace_back(std::move(field_ids));
  return index;
}

size_t DexHelper::CreateMethodIndex(size_t dex_idx, uint32_t method_id) const {
  auto &dex = readers_[dex_idx];
  auto &strs = strings_[dex_idx];
  auto &method = dex.MethodIds()[method_id];
  auto &params = method_params_[dex_idx][method_id];
  std::vector<std::string_view> param_names;
  if (params) {
    param_names.reserve(params->size);
    for (size_t i = 0; i < params->size; ++i) {
      param_names.emplace_back(
          strs[dex.TypeIds()[params->list[i].type_idx].descriptor_idx]);
    }
  }
  return CreateMethodIndex(strs[dex.TypeIds()[method.class_idx].descriptor_idx],
                           strs[method.name_idx], param_names);
}

size_t DexHelper::CreateClassIndex(size_t dex_idx, uint32_t class_id) const {
  auto &dex = readers_[dex_idx];
  auto &strs = strings_[dex_idx];
  return CreateClassIndex(strs[dex.TypeIds()[class_id].descriptor_idx],
                          dex_idx);
}

size_t DexHelper::CreateFieldIndex(size_t dex_idx, uint32_t field_id) const {
  auto &dex = readers_[dex_idx];
  auto &strs = strings_[dex_idx];
  auto &field = dex.FieldIds()[field_id];
  return CreateFieldIndex(strs[dex.TypeIds()[field.class_idx].descriptor_idx],
                          strs[field.name_idx], dex_idx);
}

auto DexHelper::DecodeClass(size_t class_idx) const -> Class {
  if (class_idx >= class_indices_.size())
    return {};
  auto &class_ids = class_indices_[class_idx];
  for (size_t dex_idx = 0; dex_idx < readers_.size(); ++dex_idx) {
    auto class_id = class_ids[dex_idx];
    if (class_id == dex::kNoIndex)
      continue;
    return {
        .name = strings_[dex_idx]
                        [readers_[dex_idx].TypeIds()[class_id].descriptor_idx],
    };
  }
  return {};
}

auto DexHelper::DecodeField(size_t field_idx) const -> Field {
  if (field_idx >= field_indices_.size())
    return {};
  auto &field_ids = field_indices_[field_idx];
  for (size_t dex_idx = 0; dex_idx < readers_.size(); ++dex_idx) {
    auto field_id = field_ids[dex_idx];
    if (field_id == dex::kNoIndex)
      continue;
    auto &dex = readers_[dex_idx];
    auto &field = dex.FieldIds()[field_id];
    auto &strs = strings_[dex_idx];
    return {
        .declaring_class =
            {
                .name = strs[dex.TypeIds()[field.class_idx].descriptor_idx],
            },
        .type = {.name = strs[dex.TypeIds()[field.type_idx].descriptor_idx]},
        .name = strings_[dex_idx][field.name_idx],
    };
  }
  return {};
}

auto DexHelper::DecodeMethod(size_t method_idx) const -> Method {
  if (method_idx >= method_indices_.size())
    return {};
  auto &method_ids = method_indices_[method_idx];
  for (size_t dex_idx = 0; dex_idx < readers_.size(); ++dex_idx) {
    auto method_id = method_ids[dex_idx];
    if (method_id == dex::kNoIndex)
      continue;
    auto &dex = readers_[dex_idx];
    auto &method = dex.MethodIds()[method_id];
    auto &strs = strings_[dex_idx];
    std::vector<Class> parameters;
    auto &params = method_params_[dex_idx][method_id];
    size_t params_size = params ? params->size : 0;
    for (size_t i = 0; i < params_size; ++i) {
      parameters.emplace_back(Class{
          .name = strs[dex.TypeIds()[params->list[i].type_idx].descriptor_idx],
      });
    }
    return {
        .declaring_class =
            {
                .name = strs[dex.TypeIds()[method.class_idx].descriptor_idx],
            },
        .name = strs[method.name_idx],
        .parameters = std::move(parameters),
        .return_type = {
            .name = strs
                [dex.TypeIds()[dex.ProtoIds()[method.proto_idx].return_type_idx]
                     .descriptor_idx]}};
  }
  return {};
};

std::vector<size_t>
DexHelper::GetPriority(const std::vector<size_t> &priority) const {
  std::vector<size_t> out;
  if (priority.empty()) {
    for (size_t i = 0; i < readers_.size(); ++i) {
      out.emplace_back(i);
    }
  } else {
    for (auto &i : priority) {
      if (i < readers_.size()) {
        out.emplace_back(i);
      }
    }
  }
  return out;
}
