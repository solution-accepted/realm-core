/*************************************************************************
 *
 * Copyright 2016 Realm Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 **************************************************************************/

#ifndef REALM_ARRAY_KEY_HPP
#define REALM_ARRAY_KEY_HPP

#include <realm/array.hpp>
#include <realm/cluster.hpp>

namespace realm {

class ArrayKey : public ArrayPayload, private Array {
public:
    using value_type = Key;

    using Array::Array;
    using Array::set_parent;
    using Array::init_from_parent;
    using Array::update_parent;
    using Array::get_ref;
    using Array::size;
    using Array::erase;

    static Key default_value(bool)
    {
        return {};
    }

    void init_from_ref(ref_type ref) noexcept override
    {
        Array::init_from_ref(ref);
    }

    void create()
    {
        Array::create(type_Normal);
    }

    void add(Key value)
    {
        Array::add(value.value + 1);
    }
    void set(size_t ndx, Key value)
    {
        Array::set(ndx, value.value + 1);
    }

    void set_null(size_t ndx)
    {
        Array::set(ndx, 0);
    }
    void insert(size_t ndx, Key value)
    {
        Array::insert(ndx, value.value + 1);
    }
    Key get(size_t ndx) const
    {
        return Key{Array::get(ndx) - 1};
    }
    std::vector<Key> get_all() const
    {
        size_t sz = size();
        std::vector<Key> keys;
        keys.reserve(sz);
        for (size_t i = 0; i < sz; i++) {
            keys.push_back(get(i));
        }
        return keys;
    }
    bool is_null(size_t ndx) const
    {
        return Array::get(ndx) == 0;
    }
    void truncate_and_destroy_children(size_t ndx)
    {
        Array::truncate(ndx);
    }

    size_t find_first(Key value, size_t begin, size_t end) const noexcept
    {
        return Array::find_first(value.value + 1, begin, end);
    }

    void nullify(Key key)
    {
        size_t begin = find_first(key, 0, Array::size());
        // There must be one
        REALM_ASSERT(begin != realm::npos);
        Array::erase(begin);
    }
};
}

#endif /* SRC_REALM_ARRAY_KEY_HPP_ */
