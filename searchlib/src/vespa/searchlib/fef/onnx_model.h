// Copyright Verizon Media. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#pragma once

#include <vespa/vespalib/stllike/string.h>
#include <optional>
#include <map>

namespace search::fef {

/**
 * Class containing configuration for a single onnx model setup. This
 * class is used both by the IIndexEnvironment api as well as the
 * OnnxModels config adapter in the search core (matching component).
 **/
class OnnxModel {
private:
    vespalib::string _name;
    vespalib::string _file_path;
    std::map<vespalib::string,vespalib::string> _input_features;
    std::map<vespalib::string,vespalib::string> _output_names;

public:
    OnnxModel(const vespalib::string &name_in,
              const vespalib::string &file_path_in);
    ~OnnxModel();

    const vespalib::string &name() const { return _name; }    
    const vespalib::string &file_path() const { return _file_path; }
    OnnxModel &input_feature(const vespalib::string &model_input_name, const vespalib::string &input_feature);
    OnnxModel &output_name(const vespalib::string &model_output_name, const vespalib::string &output_name);
    std::optional<vespalib::string> input_feature(const vespalib::string &model_input_name) const;
    std::optional<vespalib::string> output_name(const vespalib::string &model_output_name) const;
    bool operator==(const OnnxModel &rhs) const;
    const std::map<vespalib::string,vespalib::string> &inspect_input_features() const { return _input_features; }
    const std::map<vespalib::string,vespalib::string> &inspect_output_names() const { return _output_names; }
};

}
