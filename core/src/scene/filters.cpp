#include "filters.h"
#include "scene/styleContext.h"
#include "data/tileData.h"
#include "platform.h"

#include <cmath>

namespace Tangram {

void Filter::print(int _indent) const {

    switch (data.get_type_index()) {

    case Data::type<OperatorAny>::value: {
        logMsg("%*s any\n", _indent, "");
        for (const auto& filt : data.get<OperatorAny>().operands) {
            filt.print(_indent + 2);
        }
        break;
    }
    case Data::type<OperatorAll>::value: {
        logMsg("%*s all\n", _indent, "");
        for (const auto& filt : data.get<OperatorAll>().operands) {
            filt.print(_indent + 2);
        }
        break;
    }
    case Data::type<OperatorNone>::value: {
        logMsg("%*s none\n", _indent, "");
        for (const auto& filt : data.get<OperatorNone>().operands) {
            filt.print(_indent + 2);
        }
        break;
    }
    case Data::type<Existence>::value: {
        auto& f = data.get<Existence>();
        logMsg("%*s existence - key:%s\n", _indent, "", f.key.c_str());
        break;
    }
    case Data::type<EqualitySet>::value: {
        auto& f = data.get<EqualitySet>();
        if (f.values[0].is<std::string>()) {
            logMsg("%*s equality set - global:%d key:%s val:%s\n", _indent, "",
                   f.global != FilterGlobal::undefined,
                   f.key.c_str(),
                   f.values[0].get<std::string>().c_str());
        }
        if (f.values[0].is<double>()) {
            logMsg("%*s equality - global:%d key:%s val:%f\n", _indent, "",
                   f.global != FilterGlobal::undefined,
                   f.key.c_str(),
                   f.values[0].get<double>());
        }
        break;
    }
    case Data::type<Equality>::value: {
        auto& f = data.get<Equality>();
        if (f.value.is<std::string>()) {
            logMsg("%*s equality - global:%d key:%s val:%s\n", _indent, "",
                   f.global != FilterGlobal::undefined,
                   f.key.c_str(),
                   f.value.get<std::string>().c_str());
        }
        if (f.value.is<double>()) {
            logMsg("%*s equality - global:%d key:%s val:%f\n", _indent, "",
                   f.global != FilterGlobal::undefined,
                   f.key.c_str(),
                   f.value.get<double>());
        }
        break;
    }
    case Data::type<Range>::value: {
        auto& f = data.get<Range>();
        logMsg("%*s range - global:%d key:%s min:%f max:%f\n", _indent, "",
               f.global != FilterGlobal::undefined,
               f.key.c_str(), f.min, f.max);
        return;
    }
    case Data::type<Function>::value: {
        logMsg("%*s function\n", _indent, "");
        break;
    }
    default:
        break;
    }

}


int Filter::filterCost() const {
    // Add some extra penalty for set vs simple filters
    int sum = 100;

    switch (data.get_type_index()) {
    case Data::type<OperatorAny>::value:
        for (auto& f : operands()) { sum += f.filterCost(); }
        return sum;

    case Data::type<OperatorAll>::value:
        for (auto& f : operands()) { sum += f.filterCost(); }
        return sum;

    case Data::type<OperatorNone>::value:
        for (auto& f : operands()) { sum += f.filterCost(); }
        return sum;

    case Data::type<Existence>::value:
        // Equality and Range are more specific for increasing
        // the chance to fail early check them before Existence
        return 20;

    case Data::type<EqualitySet>::value:
        return data.get<EqualitySet>().global == FilterGlobal::undefined ? 10 : 1;

    case Data::type<Equality>::value:
        return data.get<Equality>().global == FilterGlobal::undefined ? 10 : 1;

    case Data::type<Filter::Range>::value:
        return data.get<Range>().global == FilterGlobal::undefined ? 10 : 1;

    case Data::type<Function>::value:
        // Most expensive filter should be checked last
        return 1000;
    }
    assert(false);
    return 0;
}

const std::string& Filter::key() const {
    static const std::string empty = "";

    switch (data.get_type_index()) {

    case Data::type<Existence>::value:
        return data.get<Existence>().key;

    case Data::type<EqualitySet>::value:
        return data.get<EqualitySet>().key;

    case Data::type<Equality>::value:
        return data.get<Equality>().key;

    case Data::type<Filter::Range>::value:
        return data.get<Range>().key;

    default:
        break;
    }
    return empty;
}

const std::vector<Filter>& Filter::operands() const {
    static const std::vector<Filter> empty;

    switch (data.get_type_index()) {
    case Data::type<OperatorAny>::value:
        return data.get<OperatorAny>().operands;

    case Data::type<OperatorAll>::value:
        return data.get<OperatorAll>().operands;

    case Data::type<OperatorNone>::value:
        return data.get<OperatorNone>().operands;

    default:
        break;
    }
    return empty;
}

const bool Filter::isOperator() const {

    switch (data.get_type_index()) {
    case Data::type<OperatorAny>::value:
        return true;

    case Data::type<OperatorAll>::value:
        return true;

    case Data::type<OperatorNone>::value:
        return true;

    default:
        break;
    }
    return false;
}

int compareSetFilter(const Filter& a, const Filter& b) {
    auto& oa = a.operands();
    auto& ob = b.operands();

    if (oa.size() != ob.size()) { return oa.size() < ob.size(); }

    if (oa[0].data.is<Filter::Range>() &&
        ob[0].data.is<Filter::Range>() &&
        oa[0].key() == ob[0].key()) {
        // take the one with more restrictive range
        auto ra = oa[0].data.get<Filter::Range>();
        auto rb = ob[0].data.get<Filter::Range>();

        if (std::isinf(ra.max) && std::isinf(rb.max)) {
            return rb.min - ra.min;
        }
    }

    return 0;
}

std::vector<Filter> Filter::sort(const std::vector<Filter>& _filters) {
    std::vector<Filter> filters = _filters;
    std::sort(filters.begin(), filters.end(),
              [](auto& a, auto& b) {

                  // Sort simple filters by eval cost
                  int ma = a.filterCost();
                  int mb = b.filterCost();

                  if (!a.isOperator() && !b.isOperator()) {
                      int diff = ma - mb;
                      if (diff != 0) {
                          return diff < 0;
                      }

                      // just for consistent ordering
                      // (and using > to prefer $zoom over $geom)
                      return a.key() > b.key();
                  }

                  // When one is a simple Filter and the other is a operaor
                  // or both are operators prefer the one with the cheaper
                  // filter(s).
                  if (ma != mb) {
                      return ma < mb;
                  }

                  return compareSetFilter(a, b) < 0;
              });

    return filters;
}


struct string_matcher {
    using result_type = bool;
    const std::string& str;

    template <typename T>
    bool operator()(T v) const { return false; }
    bool operator()(const std::string& v) const {
        return str == v;
    }
};

struct number_matcher {
    using result_type = bool;
    double num;

    template <typename T>
    bool operator()(T v) const { return false; }
    bool operator()(const double& v) const {
        if (num == v) { return true; }
        return std::fabs(num - v) <= std::numeric_limits<double>::epsilon();
    }
};

struct match_equal_set {
    using result_type = bool;
    const std::vector<Value>& values;

    template <typename T>
    bool operator()(T) const { return false; }

    bool operator()(const double& num) const {
        number_matcher m{num};
        for (const auto& v : values) {
            if (Value::visit(v, m)) {
                return true;
            }
        }
        return false;
    }
    bool operator()(const std::string& str) const {
        string_matcher m{str};

        for (const auto& v : values) {
            if (Value::visit(v, m)) {
                return true;
            }
        }
        return false;
    }
};

struct match_equal {
    using result_type = bool;
    const Value& value;

    template <typename T>
    bool operator()(T) const { return false; }

    bool operator()(const double& num) const {
        return Value::visit(value, number_matcher{num});
    }
    bool operator()(const std::string& str) const {
        return Value::visit(value, string_matcher{str});
    }
};

struct match_range {
    const Filter::Range& f;

    bool operator() (const double& num) const {
        return num >= f.min && num < f.max;
    }
    bool operator() (const std::string&) const { return false; }
    bool operator() (const none_type&) const { return false; }
};

struct matcher {
    using result_type = bool;

    matcher(const Feature& feat, StyleContext& ctx) :
        props(feat.props), ctx(ctx) {}

    const Properties& props;
    StyleContext& ctx;

    bool eval(const Filter::Data& data) const {
        return Filter::Data::visit(data, *this);
    }

    bool operator() (const Filter::OperatorAny& f) const {
        for (const auto& filt : f.operands) {
            if (eval(filt.data)) { return true; }
        }
        return false;
    }
    bool operator() (const Filter::OperatorAll& f) const {
        for (const auto& filt : f.operands) {
            if (!eval(filt.data)) { return false; }
        }
        return true;
    }
    bool operator() (const Filter::OperatorNone& f) const {
        for (const auto& filt : f.operands) {
            if (eval(filt.data)) { return false; }
        }
        return true;
    }
    bool operator() (const Filter::Existence& f) const {
        return f.exists == props.contains(f.key);
    }
    bool operator() (const Filter::EqualitySet& f) const {
        auto& value = (f.global == FilterGlobal::undefined)
            ? props.get(f.key)
            : ctx.getGlobal(f.global);

        return Value::visit(value, match_equal_set{f.values});
    }
    bool operator() (const Filter::Equality& f) const {
        auto& value = (f.global == FilterGlobal::undefined)
            ? props.get(f.key)
            : ctx.getGlobal(f.global);

        return Value::visit(value, match_equal{f.value});
    }
    bool operator() (const Filter::Range& f) const {
        auto& value = (f.global == FilterGlobal::undefined)
            ? props.get(f.key)
            : ctx.getGlobal(f.global);

        return Value::visit(value, match_range{f});
    }
    bool operator() (const Filter::Function& f) const {
        return ctx.evalFilter(f.id);
    }
    bool operator() (const none_type& f) const {
        return true;
    }
};

bool Filter::eval(const Feature& feat, StyleContext& ctx) const {
    return Data::visit(data, matcher(feat, ctx));
}

}
