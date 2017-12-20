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

#ifndef REALM_TABLE_HPP
#define REALM_TABLE_HPP

#include <algorithm>
#include <map>
#include <utility>
#include <typeinfo>
#include <memory>
#include <mutex>

#include <realm/util/features.h>
#include <realm/util/thread.hpp>
#include <realm/table_ref.hpp>
#include <realm/link_view_fwd.hpp>
#include <realm/list.hpp>
#include <realm/spec.hpp>
#include <realm/query.hpp>
#include <realm/column.hpp>
#include <realm/cluster_tree.hpp>
#include <realm/keys.hpp>

namespace realm {

class BacklinkColumn;
class BinaryColumy;
class ConstTableView;
class Group;
class LinkColumn;
class LinkColumnBase;
class LinkListColumn;
class LinkView;
class SortDescriptor;
class StringIndex;
class TableView;
class TableViewBase;
class TimestampColumn;
template <class>
class Columns;
template <class>
class SubQuery;
struct LinkTargetInfo;

struct Link {
};
typedef Link BackLink;


namespace _impl {
class TableFriend;
}
namespace metrics {
class QueryInfo;
}

class Replication;

/// FIXME: Table assignment (from any group to any group) could be made aliasing
/// safe as follows: Start by cloning source table into target allocator. On
/// success, assign, and then deallocate any previous structure at the target.
///
/// FIXME: It might be desirable to have a 'table move' feature between two
/// places inside the same group (say from a subtable or a mixed column to group
/// level). This could be done in a very efficient manner.
///
/// FIXME: When compiling in debug mode, all public non-static table functions
/// should REALM_ASSERT(is_attached()).
class Table {
public:
    /// Construct a new freestanding top-level table with static
    /// lifetime.
    ///
    /// This constructor should be used only when placing a table
    /// instance on the stack, and it is then the responsibility of
    /// the application that there are no objects of type TableRef or
    /// ConstTableRef that refer to it, or to any of its subtables,
    /// when it goes out of scope. To create a top-level table with
    /// dynamic lifetime, use Table::create() instead.
    Table(Allocator& = Allocator::get_default());

    /// Construct a copy of the specified table as a new freestanding
    /// top-level table with static lifetime.
    ///
    /// This constructor should be used only when placing a table
    /// instance on the stack, and it is then the responsibility of
    /// the application that there are no objects of type TableRef or
    /// ConstTableRef that refer to it, or to any of its subtables,
    /// when it goes out of scope. To create a top-level table with
    /// dynamic lifetime, use Table::copy() instead.
    Table(const Table&, Allocator& = Allocator::get_default());

    ~Table() noexcept;

    Allocator& get_alloc() const;

    /// Construct a new freestanding top-level table with dynamic lifetime.
    static TableRef create(Allocator& = Allocator::get_default());

    /// Construct a copy of the specified table as a new freestanding top-level
    /// table with dynamic lifetime.
    TableRef copy(Allocator& = Allocator::get_default()) const;

    /// Returns true if, and only if this accessor is currently attached to an
    /// underlying table.
    ///
    /// A table accessor may get detached from the underlying row for various
    /// reasons (see below). When it does, it no longer refers to anything, and
    /// can no longer be used, except for calling is_attached(). The
    /// consequences of calling other non-static functions on a detached table
    /// accessor are unspecified. Table accessors obtained by calling functions in
    /// the Realm API are always in the 'attached' state immediately upon
    /// return from those functions.
    ///
    /// A table accessor of a free-standing table never becomes detached (except
    /// during its eventual destruction). A group-level table accessor becomes
    /// detached if the underlying table is removed from the group, or when the
    /// group accessor is destroyed. A subtable accessor becomes detached if the
    /// underlying subtable is removed, or if the parent table accessor is
    /// detached. A table accessor does not become detached for any other reason
    /// than those mentioned here.
    ///
    /// FIXME: High level language bindings will probably want to be able to
    /// explicitely detach a group and all tables of that group if any modifying
    /// operation fails (e.g. memory allocation failure) (and something similar
    /// for freestanding tables) since that leaves the group in state where any
    /// further access is disallowed. This way they will be able to reliably
    /// intercept any attempt at accessing such a failed group.
    ///
    /// FIXME: The C++ documentation must state that if any modifying operation
    /// on a group (incl. tables, subtables, and specs) or on a free standing
    /// table (incl. subtables and specs) fails, then any further access to that
    /// group (except ~Group()) or freestanding table (except ~Table()) has
    /// undefined behaviour and is considered an error on behalf of the
    /// application. Note that even Table::is_attached() is disallowed in this
    /// case.
    bool is_attached() const noexcept;

    /// Get the name of this table, if it has one. Only group-level tables have
    /// names. For a table of any other kind, this function returns the empty
    /// string.
    StringData get_name() const noexcept;

    // Whether or not elements can be null.
    bool is_nullable(size_t col_ndx) const;

    //@{
    /// Conventience functions for inspecting the dynamic table type.
    ///
    /// These functions behave as if they were called on the descriptor returned
    /// by get_descriptor().
    size_t get_column_count() const noexcept;
    DataType get_column_type(size_t column_ndx) const noexcept;
    StringData get_column_name(size_t column_ndx) const noexcept;
    size_t get_column_index(StringData name) const noexcept;
    //@}

    //@{
    /// Convenience functions for manipulating the dynamic table type.
    ///
    /// These function must be called only for tables with independent dynamic
    /// type. A table has independent dynamic type if the function
    /// has_shared_type() returns false. A table that is a direct member of a
    /// group has independent dynamic type. So does a free-standing table, and a
    /// subtable in a column of type 'mixed'. All other tables have shared
    /// dynamic type. The consequences of calling any of these functions for a
    /// table with shared dynamic type are undefined.
    ///
    /// Apart from that, these functions behave as if they were called on the
    /// descriptor returned by get_descriptor(). Note especially that the
    /// `_link` suffixed functions must be used when inserting link-type
    /// columns.
    ///
    /// If you need to change the shared dynamic type of the subtables in a
    /// subtable column, consider using the API offered by the Descriptor class.
    ///
    /// \sa has_shared_type()
    /// \sa get_descriptor()

    static const size_t max_column_name_length = 63;

    size_t add_column(DataType type, StringData name, bool nullable = false);
    size_t add_column_list(DataType type, StringData name);
    void insert_column(size_t column_ndx, DataType type, StringData name, bool nullable = false);

    size_t add_column_link(DataType type, StringData name, Table& target, LinkType link_type = link_Weak);
    void insert_column_link(size_t column_ndx, DataType type, StringData name, Table& target,
                            LinkType link_type = link_Weak);
    void remove_column(size_t column_ndx);
    void rename_column(size_t column_ndx, StringData new_name);
    //@}

    /// There are two kinds of links, 'weak' and 'strong'. A strong link is one
    /// that implies ownership, i.e., that the origin row (parent) owns the
    /// target row (child). Simply stated, this means that when the origin row
    /// (parent) is removed, so is the target row (child). If there are multiple
    /// strong links to a target row, the origin rows share ownership, and the
    /// target row is removed when the last owner disappears. Weak links do not
    /// imply ownership, and will be nullified or removed when the target row
    /// disappears.
    ///
    /// To put this in precise terms; when a strong link is broken, and the
    /// target row has no other strong links to it, the target row is removed. A
    /// row that is implicitly removed in this way, is said to be
    /// *cascade-removed*. When a weak link is broken, nothing is
    /// cascade-removed.
    ///
    /// A link is considered broken if
    ///
    ///  - the link is nullified, removed, or replaced by a different link
    ///    (Row::nullify_link(), Row::set_link(), LinkView::remove_link(),
    ///    LinkView::set_link(), LinkView::clear()), or if
    ///
    ///  - the origin row is explicitly removed (Row::move_last_over(),
    ///    Table::clear()), or if
    ///
    ///  - the origin row is cascade-removed, or if
    ///
    ///  - the origin column is removed from the table (Table::remove_column()),
    ///    or if
    ///
    ///  - the origin table is removed from the group.
    ///
    /// Note that a link is *not* considered broken when it is replaced by a
    /// link to the same target row. I.e., no no rows will be cascade-removed
    /// due to such an operation.
    ///
    /// When a row is explicitly removed (such as by Table::move_last_over()),
    /// all links to it are automatically removed or nullified. For single link
    /// columns (type_Link), links to the removed row are nullified. For link
    /// list columns (type_LinkList), links to the removed row are removed from
    /// the list.
    ///
    /// When a row is cascade-removed there can no longer be any strong links to
    /// it, but if there are any weak links, they will be removed or nullified.
    ///
    /// It is important to understand that this cascade-removal scheme is too
    /// simplistic to enable detection and removal of orphaned link-cycles. In
    /// this respect, it suffers from the same limitations as a reference
    /// counting scheme generally does.
    ///
    /// It is also important to understand, that the possible presence of a link
    /// cycle can cause a row to be cascade-removed as a consequence of being
    /// modified. This happens, for example, if two rows, A and B, have strong
    /// links to each other, and there are no other strong links to either of
    /// them. In this case, if A->B is changed to A->C, then both A and B will
    /// be cascade-removed. This can lead to obscure bugs in some applications,
    /// such as in the following case:
    ///
    ///     table.set_link(col_ndx_1, row_ndx, ...);
    ///     table.set_int(col_ndx_2, row_ndx, ...); // Oops, `row_ndx` may no longer refer to the same row
    ///
    /// To be safe, applications, that may encounter cycles, are advised to
    /// adopt the following pattern:
    ///
    ///     Row row = table[row_ndx];
    ///     row.set_link(col_ndx_1, ...);
    ///     if (row)
    ///         row.set_int(col_ndx_2, ...); // Ok, because we check whether the row has disappeared
    ///
    /// \param col_ndx The index of the link column (`type_Link` or
    /// `type_LinkList`) to be modified. It is an error to specify an index that
    /// is greater than, or equal to the number of columns, or to specify the
    /// index of a non-link column.
    ///
    /// \param link_type The type of links the column should store.
    void set_link_type(size_t col_ndx, LinkType);

    //@{

    /// has_search_index() returns true if, and only if a search index has been
    /// added to the specified column. Rather than throwing, it returns false if
    /// the table accessor is detached or the specified index is out of range.
    ///
    /// add_search_index() adds a search index to the specified column of the
    /// table. It has no effect if a search index has already been added to the
    /// specified column (idempotency).
    ///
    /// remove_search_index() removes the search index from the specified column
    /// of the table. It has no effect if the specified column has no search
    /// index. The search index cannot be removed from the primary key of a
    /// table.
    ///
    /// This table must be a root table; that is, it must have an independent
    /// descriptor. Freestanding tables, group-level tables, and subtables in a
    /// column of type 'mixed' are all examples of root tables. See add_column()
    /// for more on this. If you want to manipulate subtable indexes, you must use
    /// the Descriptor interface.
    ///
    /// \param column_ndx The index of a column of the table.

    bool has_search_index(size_t column_ndx) const noexcept;
    void add_search_index(size_t column_ndx);
    void remove_search_index(size_t column_ndx);

    //@}

    /// If the specified column is optimized to store only unique values, then
    /// this function returns the number of unique values currently
    /// stored. Otherwise it returns zero. This function is mainly intended for
    /// debugging purposes.
    size_t get_num_unique_values(size_t column_ndx) const;

    bool has_clusters() const
    {
        return m_clusters.is_attached();
    }

    template <class T>
    Columns<T> column(size_t column); // FIXME: Should this one have been declared noexcept?
    template <class T>
    Columns<T> column(const Table& origin, size_t origin_column_ndx);

    template <class T>
    SubQuery<T> column(size_t column, Query subquery);
    template <class T>
    SubQuery<T> column(const Table& origin, size_t origin_column_ndx, Query subquery);

    // Table size and deletion
    bool is_empty() const noexcept;
    size_t size() const noexcept;

    //@{

    /// Object handling.

    /// Create an object with key. If the key is omitted, a key will be generated by the system
    Obj create_object(Key key = {});
    /// Create a number of objects and add corresponding keys to a vector
    void create_objects(size_t number, std::vector<Key>& keys);
    /// Create a number of objects with keys supplied
    void create_objects(const std::vector<Key>& keys);
    /// Does the key refer to an object within the table?
    bool is_valid(Key key) const
    {
        return m_clusters.is_valid(key);
    }
    Obj get_object(Key key)
    {
        return m_clusters.get(key);
    }
    ConstObj get_object(Key key) const
    {
        return m_clusters.get(key);
    }
    void dump_objects()
    {
        return m_clusters.dump_objects();
    }

    bool traverse_clusters(ClusterTree::TraverseFunction func) const
    {
        return m_clusters.traverse(func);
    }

    /// remove_object() removes the specified object from the table.
    /// The removal of an object a table may cause other linked objects to be
    /// cascade-removed. The clearing of a table may also cause linked objects
    /// to be cascade-removed, but in this respect, the effect is exactly as if
    /// each object had been removed individually. See set_link_type() for details.
    void remove_object(Key key);
    /// remove_object_recursive() will delete linked rows if the removed link was the
    /// last one holding on to the row in question. This will be done recursively.
    void remove_object_recursive(Key key);
    void clear();
    using Iterator = ClusterTree::Iterator;
    using ConstIterator = ClusterTree::ConstIterator;
    ConstIterator begin() const;
    ConstIterator end() const;
    Iterator begin();
    Iterator end();
    void remove_object(ConstIterator& it)
    {
        remove_object(it->get_key());
    }
    //@}


    TableRef get_link_target(size_t column_ndx) noexcept;
    ConstTableRef get_link_target(size_t column_ndx) const noexcept;

    static const size_t max_string_size = 0xFFFFF8 - Array::header_size - 1;
    static const size_t max_binary_size = 0xFFFFF8 - Array::header_size;

    // FIXME: These limits should be chosen independently of the underlying
    // platform's choice to define int64_t and independent of the integer
    // representation. The current values only work for 2's complement, which is
    // not guaranteed by the standard.
    static constexpr int_fast64_t max_integer = std::numeric_limits<int64_t>::max();
    static constexpr int_fast64_t min_integer = std::numeric_limits<int64_t>::min();

    //@{

    /// If this accessor is attached to a subtable, then that subtable has a
    /// parent table, and the subtable either resides in a column of type
    /// `table` or of type `mixed` in that parent. In that case
    /// get_parent_table() returns a reference to the accessor associated with
    /// the parent, and get_parent_row_index() returns the index of the row in
    /// which the subtable resides. In all other cases (free-standing and
    /// group-level tables), get_parent_table() returns null and
    /// get_parent_row_index() returns realm::npos.
    ///
    /// If this accessor is attached to a subtable, and \a column_ndx_out is
    /// specified, then `*column_ndx_out` is set to the index of the column of
    /// the parent table in which the subtable resides. If this accessor is not
    /// attached to a subtable, then `*column_ndx_out` will retain its original
    /// value upon return.

    TableRef get_parent_table(size_t* column_ndx_out = nullptr) noexcept;
    ConstTableRef get_parent_table(size_t* column_ndx_out = nullptr) const noexcept;
    size_t get_parent_row_index() const noexcept;

    //@}


    /// Only group-level unordered tables can be used as origins or targets of
    /// links.
    bool is_group_level() const noexcept;

    /// If this table is a group-level table, then this function returns the
    /// index of this table within the group. Otherwise it returns realm::npos.
    size_t get_index_in_group() const noexcept;
    TableKey get_key() const noexcept;
    // Get the key of this table directly, without needing a Table accessor.
    static TableKey get_key_direct(Allocator& alloc, ref_type top_ref);

    // Aggregate functions
    size_t count_int(size_t column_ndx, int64_t value) const;
    size_t count_string(size_t column_ndx, StringData value) const;
    size_t count_float(size_t column_ndx, float value) const;
    size_t count_double(size_t column_ndx, double value) const;

    int64_t sum_int(size_t column_ndx) const;
    double sum_float(size_t column_ndx) const;
    double sum_double(size_t column_ndx) const;
    int64_t maximum_int(size_t column_ndx, Key* return_ndx = nullptr) const;
    float maximum_float(size_t column_ndx, Key* return_ndx = nullptr) const;
    double maximum_double(size_t column_ndx, Key* return_ndx = nullptr) const;
    Timestamp maximum_timestamp(size_t column_ndx, Key* return_ndx = nullptr) const;
    int64_t minimum_int(size_t column_ndx, Key* return_ndx = nullptr) const;
    float minimum_float(size_t column_ndx, Key* return_ndx = nullptr) const;
    double minimum_double(size_t column_ndx, Key* return_ndx = nullptr) const;
    Timestamp minimum_timestamp(size_t column_ndx, Key* return_ndx = nullptr) const;
    double average_int(size_t column_ndx, size_t* value_count = nullptr) const;
    double average_float(size_t column_ndx, size_t* value_count = nullptr) const;
    double average_double(size_t column_ndx, size_t* value_count = nullptr) const;

    // Searching
    template <class T>
    Key find_first(size_t column_ndx, T value) const;

    Key find_first_link(size_t target_row_index) const;
    Key find_first_int(size_t column_ndx, int64_t value) const;
    Key find_first_bool(size_t column_ndx, bool value) const;
    Key find_first_olddatetime(size_t column_ndx, OldDateTime value) const;
    Key find_first_timestamp(size_t column_ndx, Timestamp value) const;
    Key find_first_float(size_t column_ndx, float value) const;
    Key find_first_double(size_t column_ndx, double value) const;
    Key find_first_string(size_t column_ndx, StringData value) const;
    Key find_first_binary(size_t column_ndx, BinaryData value) const;
    Key find_first_null(size_t column_ndx) const;

    TableView find_all_link(Key target_key);
    ConstTableView find_all_link(Key target_key) const;
    TableView find_all_int(size_t column_ndx, int64_t value);
    ConstTableView find_all_int(size_t column_ndx, int64_t value) const;
    TableView find_all_bool(size_t column_ndx, bool value);
    ConstTableView find_all_bool(size_t column_ndx, bool value) const;
    TableView find_all_olddatetime(size_t column_ndx, OldDateTime value);
    ConstTableView find_all_olddatetime(size_t column_ndx, OldDateTime value) const;
    TableView find_all_float(size_t column_ndx, float value);
    ConstTableView find_all_float(size_t column_ndx, float value) const;
    TableView find_all_double(size_t column_ndx, double value);
    ConstTableView find_all_double(size_t column_ndx, double value) const;
    TableView find_all_string(size_t column_ndx, StringData value);
    ConstTableView find_all_string(size_t column_ndx, StringData value) const;
    TableView find_all_binary(size_t column_ndx, BinaryData value);
    ConstTableView find_all_binary(size_t column_ndx, BinaryData value) const;
    TableView find_all_null(size_t column_ndx);
    ConstTableView find_all_null(size_t column_ndx) const;

    /// The following column types are supported: String, Integer, OldDateTime, Bool
    TableView get_distinct_view(size_t column_ndx);
    ConstTableView get_distinct_view(size_t column_ndx) const;

    TableView get_sorted_view(size_t column_ndx, bool ascending = true);
    ConstTableView get_sorted_view(size_t column_ndx, bool ascending = true) const;

    TableView get_sorted_view(SortDescriptor order);
    ConstTableView get_sorted_view(SortDescriptor order) const;

    TableView get_backlink_view(Key key, Table* src_table, size_t src_col_ndx);

    /// Report the current versioning counter for the table. The versioning counter is guaranteed to
    /// change when the contents of the table changes after advance_read() or promote_to_write(), or
    /// immediately after calls to methods which change the table. The term "change" means "change of
    /// value": The storage layout of the table may change, for example due to optimization, but this
    /// is not considered a change of a value. This means that you *cannot* use a non-changing version
    /// count to indicate that object addresses (e.g. strings, binary data) remain the same.
    /// The versioning counter *may* change (but is not required to do so) when another table linked
    /// from this table, or linking to this table, is changed. The version counter *may* also change
    /// without any apparent reason.
    uint_fast64_t get_version_counter() const noexcept;

private:
    template <class T>
    TableView find_all(size_t column_ndx, T value);

public:
    //@{
    /// Find the lower/upper bound according to a column that is
    /// already sorted in ascending order.
    ///
    /// For an integer column at index 0, and an integer value '`v`',
    /// lower_bound_int(0,v) returns the index '`l`' of the first row
    /// such that `get_int(0,l) &ge; v`, and upper_bound_int(0,v)
    /// returns the index '`u`' of the first row such that
    /// `get_int(0,u) &gt; v`. In both cases, if no such row is found,
    /// the returned value is the number of rows in the table.
    ///
    ///     3 3 3 4 4 4 5 6 7 9 9 9
    ///     ^     ^     ^     ^     ^
    ///     |     |     |     |     |
    ///     |     |     |     |      -- Lower and upper bound of 15
    ///     |     |     |     |
    ///     |     |     |      -- Lower and upper bound of 8
    ///     |     |     |
    ///     |     |      -- Upper bound of 4
    ///     |     |
    ///     |      -- Lower bound of 4
    ///     |
    ///      -- Lower and upper bound of 1
    ///
    /// These functions are similar to std::lower_bound() and
    /// std::upper_bound().
    ///
    /// The string versions assume that the column is sorted according
    /// to StringData::operator<().
    size_t lower_bound_int(size_t column_ndx, int64_t value) const noexcept;
    size_t upper_bound_int(size_t column_ndx, int64_t value) const noexcept;
    size_t lower_bound_bool(size_t column_ndx, bool value) const noexcept;
    size_t upper_bound_bool(size_t column_ndx, bool value) const noexcept;
    size_t lower_bound_float(size_t column_ndx, float value) const noexcept;
    size_t upper_bound_float(size_t column_ndx, float value) const noexcept;
    size_t lower_bound_double(size_t column_ndx, double value) const noexcept;
    size_t upper_bound_double(size_t column_ndx, double value) const noexcept;
    size_t lower_bound_string(size_t column_ndx, StringData value) const noexcept;
    size_t upper_bound_string(size_t column_ndx, StringData value) const noexcept;
    //@}

    // Queries
    // Using where(tv) is the new method to perform queries on TableView. The 'tv' can have any order; it does not
    // need to be sorted, and, resulting view retains its order.
    Query where(TableViewBase* tv = nullptr)
    {
        return Query(*this, tv);
    }

    // FIXME: We need a ConstQuery class or runtime check against modifications in read transaction.
    Query where(TableViewBase* tv = nullptr) const
    {
        return Query(*this, tv);
    }

    // Perform queries on a LinkView. The returned Query holds a reference to list.
    Query where(const LinkListPtr& list)
    {
        return Query(*this, list);
    }

    Table& link(size_t link_column);
    Table& backlink(const Table& origin, size_t origin_col_ndx);

    // Optimizing. enforce == true will enforce enumeration of all string columns;
    // enforce == false will auto-evaluate if they should be enumerated or not
    void optimize(bool enforce = false);

    /// Write this table (or a slice of this table) to the specified
    /// output stream.
    ///
    /// The output will have the same format as any other Realm
    /// database file, such as those produced by Group::write(). In
    /// this case, however, the resulting database file will contain
    /// exactly one table, and that table will contain only the
    /// specified slice of the source table (this table).
    ///
    /// The new table will always have the same dynamic type (see
    /// Descriptor) as the source table (this table), and unless it is
    /// overridden (\a override_table_name), the new table will have
    /// the same name as the source table (see get_name()). Indexes
    /// (see add_search_index()) will not be carried over to the new
    /// table.
    ///
    /// \param out The destination output stream buffer.
    ///
    /// \param offset Index of first row to include (if `slice_size >
    /// 0`). Must be less than, or equal to size().
    ///
    /// \param slice_size Number of rows to include. May be zero. If
    /// `slice_size > size() - offset`, then the effective size of
    /// the written slice will be `size() - offset`.
    ///
    /// \param override_table_name Custom name to write out instead of
    /// the actual table name.
    ///
    /// \throw std::out_of_range If `offset > size()`.
    ///
    /// FIXME: While this function does provided a maximally efficient
    /// way of serializing part of a table, it offers little in terms
    /// of general utility. This is unfortunate, because it pulls
    /// quite a large amount of code into the core library to support
    /// it.
    void write(std::ostream& out, size_t offset = 0, size_t slice_size = npos,
               StringData override_table_name = StringData()) const;

    // Conversion
    void to_json(std::ostream& out, size_t link_depth = 0,
                 std::map<std::string, std::string>* renames = nullptr) const;
    void to_string(std::ostream& out, size_t limit = 500) const;
    void row_to_string(Key key, std::ostream& out) const;

    // Get a reference to this table
    TableRef get_table_ref()
    {
        return TableRef(this);
    }
    ConstTableRef get_table_ref() const
    {
        return ConstTableRef(this);
    }

    /// \brief Compare two tables for equality.
    ///
    /// Two tables are equal if they have equal descriptors
    /// (`Descriptor::operator==()`) and equal contents. Equal descriptors imply
    /// that the two tables have the same columns in the same order. Equal
    /// contents means that the two tables must have the same number of rows,
    /// and that for each row index, the two rows must have the same values in
    /// each column.
    ///
    /// In mixed columns, both the value types and the values are required to be
    /// equal.
    ///
    /// For a particular row and column, if the two values are themselves tables
    /// (subtable and mixed columns) value equality implies a recursive
    /// invocation of `Table::operator==()`.
    bool operator==(const Table&) const;

    /// \brief Compare two tables for inequality.
    ///
    /// See operator==().
    bool operator!=(const Table& t) const;

    /// Compute the sum of the sizes in number of bytes of all the array nodes
    /// that currently make up this table. See also
    /// Group::compute_aggregate_byte_size().
    ///
    /// If this table accessor is the detached state, this function returns
    /// zero.
    size_t compute_aggregated_byte_size() const noexcept;

    // Debug
    void verify() const;
#ifdef REALM_DEBUG
    void to_dot(std::ostream&, StringData title = StringData()) const;
    void print() const;
    MemStats stats() const;
    void dump_node_structure() const; // To std::cerr (for GDB)
    void dump_node_structure(std::ostream&, int level) const;
#endif

    class Parent;
    using HandoverPatch = TableHandoverPatch;
    static void generate_patch(const Table* ref, std::unique_ptr<HandoverPatch>& patch);
    static TableRef create_from_and_consume_patch(std::unique_ptr<HandoverPatch>& patch, Group& group);

protected:
    /// Compare the objects of two tables under the assumption that the two tables
    /// have the same number of columns, and the same data type at each column
    /// index (as expressed through the DataType enum).
    bool compare_objects(const Table&) const;

    void check_lists_are_empty(size_t row_ndx) const;

private:
    class SliceWriter;

    // Number of rows in this table
    size_t m_size;

    // Underlying array structure. `m_top` is in use only for root tables; that
    // is, for tables with independent descriptor. `m_columns` contains a ref
    // for each column and search index in order of the columns. A search index
    // ref always occurs immediately after the ref of the column to which the
    // search index belongs.
    //
    // A subtable column (a column of type `type_table`) is essentially just a
    // column of 'refs' pointing to the root node of each subtable.
    //
    // To save space in the database file, a subtable in such a column always
    // starts out in a degenerate form where nothing is allocated on its behalf,
    // and a null 'ref' is stored in the corresponding slot of the column. A
    // subtable remains in this degenerate state until the first row is added to
    // the subtable.
    //
    // For this scheme to work, it must be (and is) possible to create a table
    // accessor that refers to a degenerate subtable. A table accessor (instance
    // of `Table`) refers to a degenerate subtable if, and only if `m_columns`
    // is unattached.
    //
    // FIXME: The fact that `m_columns` may be detached means that many
    // functions (even non-modifying functions) need to check for that before
    // accessing the contents of the table. This incurs a runtime
    // overhead. Consider whether this overhead can be eliminated by having
    // `Table::m_columns` always attached to something, and then detect the
    // degenerate state in a different way.
    Array m_top;
    Array m_columns; // 2nd slot in m_top (for root tables)

    using SpecPtr = std::unique_ptr<Spec>;
    SpecPtr m_spec; // 1st slot in m_top (for root tables)
    ClusterTree m_clusters;
    int64_t m_next_key_value = -1;
    TableKey m_key;

    // Is guaranteed to be empty for a detached accessor. Otherwise it is empty
    // when the table accessor is attached to a degenerate subtable (unattached
    // `m_columns`), otherwise it contains precisely one column accessor for
    // each column in the table, in order.
    //
    // In some cases an entry may be null. This is currently possible only in
    // connection with Group::advance_transact(), but it means that several
    // member functions must be prepared to handle these null entries; in
    // particular, detach(), ~Table(), functions called on behalf of detach()
    // and ~Table(), and functiones called on behalf of
    // Group::advance_transact().
    typedef std::vector<ColumnBase*> column_accessors;
    column_accessors m_cols;

    mutable std::atomic<size_t> m_ref_count;

    // Points to first bound row accessor, or is null if there are none.
    mutable RowBase* m_row_accessors = nullptr;

    // Mutex which must be locked any time the row accessor chain or m_views is used
    mutable util::Mutex m_accessor_mutex;

    // Used for queries: Items are added with link() method during buildup of query
    mutable std::vector<size_t> m_link_chain;

    /// Used only in connection with Group::advance_transact() and
    /// Table::refresh_accessor_tree().
    mutable bool m_mark;

    mutable uint_fast64_t m_version;

    void batch_erase_rows(const KeyColumn& keys);
    void do_remove_object(Key key);
    void do_clear(bool broken_reciprocal_backlinks);
    size_t do_set_link(size_t col_ndx, size_t row_ndx, size_t target_row_ndx);

    void rebuild_search_index(size_t current_file_format_version);

    /// Update the version of this table and all tables which have links to it.
    /// This causes all views referring to those tables to go out of sync, so that
    /// calls to sync_if_needed() will bring the view up to date by reexecuting the
    /// query.
    ///
    /// \param bump_global chooses whether the global versioning counter must be
    /// bumped first as part of the update. This is the normal mode of operation,
    /// when a change is made to the table. When calling recursively (following links
    /// or going to the parent table), the parameter should be set to false to correctly
    /// prune traversal.
    void bump_version(bool bump_global = true) const noexcept;

    /// Disable copying assignment.
    ///
    /// It could easily be implemented by calling assign(), but the
    /// non-checking nature of the low-level dynamically typed API
    /// makes it too risky to offer this feature as an
    /// operator.
    ///
    /// FIXME: assign() has not yet been implemented, but the
    /// intention is that it will copy the rows of the argument table
    /// into this table after clearing the original contents, and for
    /// target tables without a shared spec, it would also copy the
    /// spec. For target tables with shared spec, it would be an error
    /// to pass an argument table with an incompatible spec, but
    /// assign() would not check for spec compatibility. This would
    /// make it ideal as a basis for implementing operator=() for
    /// typed tables.
    Table& operator=(const Table&) = delete;

    /// Used when constructing an accessor whose lifetime is going to be managed
    /// by reference counting. The lifetime of accessors of free-standing tables
    /// allocated on the stack by the application is not managed by reference
    /// counting, so that is a case where this tag must **not** be specified.
    class ref_count_tag {
    };

    /// Create an uninitialized accessor whose lifetime is managed by reference
    /// counting.
    Table(ref_count_tag, Allocator&);

    void init(ref_type top_ref, ArrayParent*, size_t ndx_in_parent, bool skip_create_column_accessors = false);

    void do_insert_column(size_t col_ndx, DataType type, StringData name, LinkTargetInfo& link_target_info,
                          bool nullable = false, bool listtype = false);
    void do_insert_column_unless_exists(size_t col_ndx, DataType type, StringData name, LinkTargetInfo& link,
                                        bool nullable = false, bool listtype = false, bool* was_inserted = nullptr);

    struct InsertSubtableColumns;
    struct EraseSubtableColumns;
    struct RenameSubtableColumns;

    void insert_root_column(size_t col_ndx, DataType type, StringData name, LinkTargetInfo& link_target,
                            bool nullable = false, bool linktype = false);
    void erase_root_column(size_t col_ndx);
    void do_insert_root_column(size_t col_ndx, ColumnType, StringData name, bool nullable = false,
                               bool listtype = false);
    void do_erase_root_column(size_t col_ndx);
    void insert_backlink_column(TableKey origin_table_key, size_t origin_col_ndx, size_t backlink_col_ndx,
                                StringData name);
    void erase_backlink_column(TableKey origin_table_key, size_t origin_col_ndx);
    void update_link_target_tables(size_t old_col_ndx_begin, size_t new_col_ndx_begin);
    void update_link_target_tables_after_column_move(size_t moved_from, size_t moved_to);

    struct AccessorUpdater {
        virtual void update(Table&) = 0;
        virtual void update_parent(Table&) = 0;
        virtual ~AccessorUpdater()
        {
        }
    };
    void update_accessors(AccessorUpdater&);

    ColumnBase* create_column_accessor(ColumnType, size_t col_ndx, size_t ndx_in_parent);
    void destroy_column_accessors() noexcept;

    /// Called in the context of Group::commit() to ensure that
    /// attached table accessors stay valid across a commit. Please
    /// note that this works only for non-transactional commits. Table
    /// accessors obtained during a transaction are always detached
    /// when the transaction ends.
    void update_from_parent(size_t old_baseline) noexcept;

    // Support function for conversions
    void to_string_header(std::ostream& out, std::vector<size_t>& widths) const;
    void to_string_row(Key key, std::ostream& out, const std::vector<size_t>& widths) const;

    // recursive methods called by to_json, to follow links
    void to_json(std::ostream& out, size_t link_depth, std::map<std::string, std::string>& renames,
                 std::vector<ref_type>& followed) const;
    void to_json_row(size_t row_ndx, std::ostream& out, size_t link_depth,
                     std::map<std::string, std::string>& renames, std::vector<ref_type>& followed) const;
    void to_json_row(size_t row_ndx, std::ostream& out, size_t link_depth = 0,
                     std::map<std::string, std::string>* renames = nullptr) const;

    // Detach accessor from underlying table. Caller must ensure that
    // a reference count exists upon return, for example by obtaining
    // an extra reference count before the call.
    //
    // This function puts this table accessor into the detached
    // state. This detaches it from the underlying structure of array
    // nodes. It also recursively detaches accessors for subtables,
    // and the type descriptor accessor. When this function returns,
    // is_attached() will return false.
    //
    // This function may be called for a table accessor that is
    // already in the detached state (idempotency).
    //
    // It is also valid to call this function for a table accessor
    // that has not yet been detached, but whose underlying structure
    // of arrays have changed in an unpredictable/unknown way. This
    // kind of change generally happens when a modifying table
    // operation fails, and also when one transaction is ended and a
    // new one is started.
    void detach() noexcept;

    /// Detach and remove all attached row, link list, and subtable
    /// accessors. This function does not discard the descriptor accessor, if
    /// any, and it does not discard column accessors either.
    void discard_child_accessors() noexcept;

    void discard_row_accessors() noexcept;

    void bind_ptr() const noexcept;
    void unbind_ptr() const noexcept;

    void register_row_accessor(RowBase*) const noexcept;
    void unregister_row_accessor(RowBase*) const noexcept;
    void do_unregister_row_accessor(RowBase*) const noexcept;

    class UnbindGuard;

    ColumnType get_real_column_type(size_t column_ndx) const noexcept;

    /// If this table is a group-level table, the parent group is returned,
    /// otherwise null is returned.
    Group* get_parent_group() const noexcept;

    const ColumnBase& get_column_base(size_t column_ndx) const noexcept;
    ColumnBase& get_column_base(size_t column_ndx);

    const ColumnBaseWithIndex& get_column_base_indexed(size_t ndx) const noexcept;
    ColumnBaseWithIndex& get_column_base_indexed(size_t ndx);

    template <class T, ColumnType col_type>
    T& get_column(size_t ndx);

    template <class T, ColumnType col_type>
    const T& get_column(size_t ndx) const noexcept;

    IntegerColumn& get_column(size_t column_ndx);
    const IntegerColumn& get_column(size_t column_ndx) const noexcept;
    IntNullColumn& get_column_int_null(size_t column_ndx);
    const IntNullColumn& get_column_int_null(size_t column_ndx) const noexcept;
    FloatColumn& get_column_float(size_t column_ndx);
    const FloatColumn& get_column_float(size_t column_ndx) const noexcept;
    DoubleColumn& get_column_double(size_t column_ndx);
    const DoubleColumn& get_column_double(size_t column_ndx) const noexcept;
    StringColumn& get_column_string(size_t column_ndx);
    const StringColumn& get_column_string(size_t column_ndx) const noexcept;
    BinaryColumn& get_column_binary(size_t column_ndx);
    const BinaryColumn& get_column_binary(size_t column_ndx) const noexcept;
    StringEnumColumn& get_column_string_enum(size_t column_ndx);
    const StringEnumColumn& get_column_string_enum(size_t column_ndx) const noexcept;
    TimestampColumn& get_column_timestamp(size_t column_ndx);
    const TimestampColumn& get_column_timestamp(size_t column_ndx) const noexcept;
    const LinkColumnBase& get_column_link_base(size_t ndx) const noexcept;
    LinkColumnBase& get_column_link_base(size_t ndx);
    const LinkColumn& get_column_link(size_t ndx) const noexcept;
    LinkColumn& get_column_link(size_t ndx);
    const LinkListColumn& get_column_link_list(size_t ndx) const noexcept;
    LinkListColumn& get_column_link_list(size_t ndx);
    const BacklinkColumn& get_column_backlink(size_t ndx) const noexcept;
    BacklinkColumn& get_column_backlink(size_t ndx);

    void verify_column(size_t col_ndx) const;

    void validate_column_type(const ColumnBase& col, ColumnType expected_type, size_t ndx) const;

    static size_t get_size_from_ref(ref_type top_ref, Allocator&) noexcept;
    static size_t get_size_from_ref(ref_type spec_ref, ref_type columns_ref, Allocator&) noexcept;

    const Table* get_parent_table_ptr(size_t* column_ndx_out = nullptr) const noexcept;
    Table* get_parent_table_ptr(size_t* column_ndx_out = nullptr) noexcept;

    /// Create an empty table with independent spec and return just
    /// the reference to the underlying memory.
    static ref_type create_empty_table(Allocator&, TableKey = TableKey());

    /// Create a column of the specified type, fill it with the
    /// specified number of default values, and return just the
    /// reference to the underlying memory.
    static ref_type create_column(ColumnType column_type, size_t num_default_values, bool nullable, Allocator&);

    /// Construct a copy of the columns array of this table using the
    /// specified allocator and return just the ref to that array.
    ///
    /// In the clone, no string column will be of the enumeration
    /// type.
    ref_type clone_columns(Allocator&) const;

    /// Construct a complete copy of this table (including its spec)
    /// using the specified allocator and return just the ref to the
    /// new top array.
    ref_type clone(Allocator&) const;

    /// True for `col_type_Link` and `col_type_LinkList`.
    static bool is_link_type(ColumnType) noexcept;

    void connect_opposite_link_columns(size_t link_col_ndx, Table& target_table, size_t backlink_col_ndx) noexcept;

    void remove_recursive(CascadeState&);

    /// Used by query. Follows chain of link columns and returns final target table
    const Table* get_link_chain_target(const std::vector<size_t>& link_chain) const;

    // Precondition: 1 <= end - begin
    size_t* record_subtable_path(size_t* begin, size_t* end) const noexcept;

    /// Unless the column accessor is missing, this function returns the
    /// accessor for the target table of the specified link-type column. The
    /// column accessor is said to be missing if `m_cols[col_ndx]` is null, and
    /// this can happen only during certain operations such as the updating of
    /// the accessor tree when a read transaction is advanced. Note that for
    /// link type columns, the target table accessor exists when, and only when
    /// the origin table accessor exists. This function assumes that the
    /// specified column index in a valid index into `m_cols` and that the
    /// column is a link-type column. Beyond that, it assume nothing more than
    /// minimal accessor consistency (see AccessorConsistencyLevels.)
    Table* get_link_target_table_accessor(size_t col_ndx) const noexcept;

    void adj_insert_column(size_t col_ndx);
    void adj_erase_column(size_t col_ndx) noexcept;

    bool is_marked() const noexcept;
    void mark() noexcept;
    void unmark() noexcept;
    void recursive_mark() noexcept;
    void mark_link_target_tables(size_t col_ndx_begin) noexcept;
    void mark_opposite_link_tables() noexcept;

    Replication* get_repl() noexcept;

    void set_ndx_in_parent(size_t ndx_in_parent) noexcept;

    /// Refresh the part of the accessor tree that is rooted at this
    /// table. Subtable accessors will be refreshed only if they are marked
    /// (Table::m_mark), and this applies recursively to subtables of
    /// subtables. All refreshed table accessors (including this one) will be
    /// unmarked upon return.
    ///
    /// The following conditions are necessary and sufficient for the proper
    /// operation of this function:
    ///
    ///  - This table must be a group-level table, or a subtable. It must not be
    ///    a free-standing table (because a free-standing table has no parent).
    ///
    ///  - The `index in parent` property is correct. The `index in parent`
    ///    property of the table is the `index in parent` property of
    ///    `m_columns` for subtables with shared descriptor, and the `index in
    ///    parent` property of `m_top` for all other tables.
    ///
    ///  - If this table has shared descriptor, then the `index in parent`
    ///    property of the contained spec accessor is correct.
    ///
    ///  - The parent accessor is in a valid state (already refreshed). If the
    ///    parent is a group, then the group accessor (excluding its table
    ///    accessors) must be in a valid state. If the parent is a table, then
    ///    the table accessor (excluding its subtable accessors) must be in a
    ///    valid state.
    ///
    ///  - Every descendant subtable accessor is marked if it needs to be
    ///    refreshed, or if it has a descendant accessor that needs to be
    ///    refreshed.
    ///
    ///  - This table accessor, as well as all its descendant accessors, are in
    ///    structural correspondence with the underlying node hierarchy whose
    ///    root ref is stored in the parent (see AccessorConsistencyLevels).
    void refresh_accessor_tree();

    void refresh_column_accessors(size_t col_ndx_begin = 0);

    // Look for link columns starting from col_ndx_begin.
    // If a link column is found, follow the link and update it's
    // backlink column accessor if it is in different table.
    void refresh_link_target_accessors(size_t col_ndx_begin = 0);

    bool is_cross_table_link_target() const noexcept;
    std::recursive_mutex* get_parent_accessor_management_lock() const;
#ifdef REALM_DEBUG
    void to_dot_internal(std::ostream&) const;
#endif
    template <Action action, typename T, typename R>
    R aggregate(size_t column_ndx, T value = {}, size_t* resultcount = nullptr, Key* return_ndx = nullptr) const;
    template <typename T>
    double average(size_t column_ndx, size_t* resultcount) const;

    static constexpr int top_position_for_spec = 0;
    static constexpr int top_position_for_columns = 1;
    static constexpr int top_position_for_cluster_tree = 2;
    static constexpr int top_position_for_key = 3;

    friend class SubtableNode;
    friend class _impl::TableFriend;
    friend class Query;
    friend class metrics::QueryInfo;
    template <class>
    friend class util::bind_ptr;
    template <class>
    friend class SimpleQuerySupport;
    friend class LangBindHelper;
    friend class TableViewBase;
    template <class T>
    friend class Columns;
    friend class Columns<StringData>;
    friend class ParentNode;
    template <class>
    friend class SequentialGetter;
    friend class RowBase;
    friend class LinksToNode;
    friend class LinkMap;
    friend class LinkView;
    friend class Group;
    friend class ClusterTree;
};

class Table::Parent : public ArrayParent {
public:
    ~Parent() noexcept override
    {
    }

protected:
    virtual StringData get_child_name(size_t child_ndx) const noexcept;

    /// If children are group-level tables, then this function returns the
    /// group. Otherwise it returns null.
    virtual Group* get_parent_group() noexcept;

    /// If children are subtables, then this function returns the
    /// parent table. Otherwise it returns null.
    ///
    /// If \a column_ndx_out is not null, this function must assign the index of
    /// the column within the parent table to `*column_ndx_out` when , and only
    /// when this table parent is a column in a parent table.
    virtual Table* get_parent_table(size_t* column_ndx_out = nullptr) noexcept;

    virtual Spec* get_subtable_spec() noexcept;

    /// Must be called whenever a child table accessor is about to be destroyed.
    ///
    /// Note that the argument is a pointer to the child Table rather than its
    /// `ndx_in_parent` property. This is because only minimal accessor
    /// consistency can be assumed by this function.
    virtual void child_accessor_destroyed(Table* child) noexcept = 0;


    virtual size_t* record_subtable_path(size_t* begin, size_t* end) noexcept;
    virtual std::recursive_mutex* get_accessor_management_lock() noexcept = 0;

    friend class Table;
};


// Implementation:


inline uint_fast64_t Table::get_version_counter() const noexcept
{
    return m_version;
}

inline void Table::bump_version(bool bump_global) const noexcept
{
    if (bump_global) {
        // This is only set on initial entry through an operation on the same
        // table.  recursive calls (via parent or via backlinks) must be done
        // with bump_global=false.
        m_top.get_alloc().bump_global_version();
    }
    if (m_top.get_alloc().should_propagate_version(m_version)) {
        if (const Table* parent = get_parent_table_ptr())
            parent->bump_version(false);
        // Recurse through linked tables, use m_mark to avoid infinite recursion
        for (auto& column_ptr : m_cols) {
            // We may meet a null pointer in place of a backlink column, pending
            // replacement with a new one. This can happen ONLY when creation of
            // the corresponding forward link column in the origin table is
            // pending as well. In this case it is ok to just ignore the zeroed
            // backlink column, because the origin table is guaranteed to also
            // be refreshed/marked dirty and hence have it's version bumped.
            if (column_ptr != nullptr)
                column_ptr->bump_link_origin_table_version();
        }
    }
}

// A good place to start if you want to understand the memory ordering
// chosen for the operations below is http://preshing.com/20130922/acquire-and-release-fences/
inline void Table::bind_ptr() const noexcept
{
    m_ref_count.fetch_add(1, std::memory_order_relaxed);
}

inline void Table::unbind_ptr() const noexcept
{
    // The delete operation runs the destructor, and the destructor
    // must always see all changes to the object being deleted.
    // Within each thread, we know that unbind_ptr will always happen after
    // any changes, so it is a convenient place to do a release.
    // The release will then be observed by the acquire fence in
    // the case where delete is actually called (the count reaches 0)
    if (m_ref_count.fetch_sub(1, std::memory_order_release) != 1) {
        return;
    }

    std::atomic_thread_fence(std::memory_order_acquire);

    std::recursive_mutex* lock = get_parent_accessor_management_lock();
    if (lock) {
        std::lock_guard<std::recursive_mutex> lg(*lock);
        if (m_ref_count == 0)
            delete this;
    }
    else {
        delete this;
    }
}

inline bool Table::is_attached() const noexcept
{
    // Note that it is not possible to tie the state of attachment of a table to
    // the state of attachment of m_top, because tables with shared spec do not
    // have a 'top' array. Neither is it possible to tie it to the state of
    // attachment of m_columns, because subtables with shared spec start out in
    // a degenerate form where they do not have a 'columns' array. For these
    // reasons, it is neccessary to define the notion of attachment for a table
    // as follows: A table is attached if, and ony if m_column stores a non-null
    // parent pointer. This works because even for degenerate subtables,
    // m_columns is initialized with the correct parent pointer.
    return m_columns.has_parent();
}

inline StringData Table::get_name() const noexcept
{
    REALM_ASSERT(is_attached());
    const Array& real_top = m_top.is_attached() ? m_top : m_columns;
    ArrayParent* parent = real_top.get_parent();
    if (!parent)
        return StringData("");
    size_t index_in_parent = real_top.get_ndx_in_parent();
    REALM_ASSERT(dynamic_cast<Parent*>(parent));
    return static_cast<Parent*>(parent)->get_child_name(index_in_parent);
}

inline size_t Table::get_column_count() const noexcept
{
    REALM_ASSERT(is_attached());
    return m_spec->get_public_column_count();
}

inline StringData Table::get_column_name(size_t ndx) const noexcept
{
    REALM_ASSERT_3(ndx, <, get_column_count());
    return m_spec->get_column_name(ndx);
}

inline size_t Table::get_column_index(StringData name) const noexcept
{
    REALM_ASSERT(is_attached());
    return m_spec->get_column_index(name);
}

inline ColumnType Table::get_real_column_type(size_t ndx) const noexcept
{
    REALM_ASSERT_3(ndx, <, m_spec->get_column_count());
    return m_spec->get_column_type(ndx);
}

inline DataType Table::get_column_type(size_t ndx) const noexcept
{
    REALM_ASSERT_3(ndx, <, m_spec->get_column_count());
    return m_spec->get_public_column_type(ndx);
}

template <class Col, ColumnType col_type>
inline Col& Table::get_column(size_t ndx)
{
    ColumnBase& col = get_column_base(ndx);
#ifdef REALM_DEBUG
    validate_column_type(col, col_type, ndx);
#endif
    REALM_ASSERT(typeid(Col) == typeid(col));
    return static_cast<Col&>(col);
}

template <class Col, ColumnType col_type>
inline const Col& Table::get_column(size_t ndx) const noexcept
{
    const ColumnBase& col = get_column_base(ndx);
#ifdef REALM_DEBUG
    validate_column_type(col, col_type, ndx);
#endif
    REALM_ASSERT(typeid(Col) == typeid(col));
    return static_cast<const Col&>(col);
}

inline void Table::verify_column(size_t col_ndx) const
{
    // TODO Check against spec
    if (REALM_LIKELY(col_ndx < m_cols.size()))
        return;

    throw LogicError(LogicError::column_does_not_exist);
}

class Table::UnbindGuard {
public:
    UnbindGuard(Table* table) noexcept
        : m_table(table)
    {
    }

    ~UnbindGuard() noexcept
    {
        if (m_table)
            m_table->unbind_ptr();
    }

    Table& operator*() const noexcept
    {
        return *m_table;
    }

    Table* operator->() const noexcept
    {
        return m_table;
    }

    Table* get() const noexcept
    {
        return m_table;
    }

    Table* release() noexcept
    {
        Table* table = m_table;
        m_table = nullptr;
        return table;
    }

private:
    Table* m_table;
};


inline Table::Table(Allocator& alloc)
    : m_top(alloc)
    , m_columns(alloc)
    , m_clusters(this, alloc)
{
    m_ref_count = 1; // Explicitly managed lifetime

    ref_type ref = create_empty_table(alloc); // Throws
    Parent* parent = nullptr;
    size_t ndx_in_parent = 0;
    init(ref, parent, ndx_in_parent);
}

inline Table::Table(const Table& t, Allocator& alloc)
    : m_top(alloc)
    , m_columns(alloc)
    , m_clusters(this, alloc)
{
    m_ref_count = 1; // Explicitly managed lifetime

    ref_type ref = t.clone(alloc); // Throws
    Parent* parent = nullptr;
    size_t ndx_in_parent = 0;
    init(ref, parent, ndx_in_parent);
}

inline Table::Table(ref_count_tag, Allocator& alloc)
    : m_top(alloc)
    , m_columns(alloc)
    , m_clusters(this, alloc)
{
    m_ref_count = 0; // Lifetime managed by reference counting
}

inline Allocator& Table::get_alloc() const
{
    return m_top.get_alloc();
}

inline TableRef Table::create(Allocator& alloc)
{
    std::unique_ptr<Table> table(new Table(ref_count_tag(), alloc)); // Throws
    ref_type ref = create_empty_table(alloc);                        // Throws
    Parent* parent = nullptr;
    size_t ndx_in_parent = 0;
    table->init(ref, parent, ndx_in_parent); // Throws
    return table.release()->get_table_ref();
}

inline TableRef Table::copy(Allocator& alloc) const
{
    std::unique_ptr<Table> table(new Table(ref_count_tag(), alloc)); // Throws
    ref_type ref = clone(alloc);                                     // Throws
    Parent* parent = nullptr;
    size_t ndx_in_parent = 0;
    table->init(ref, parent, ndx_in_parent); // Throws
    return table.release()->get_table_ref();
}

// For use by queries
template <class T>
inline Columns<T> Table::column(size_t column_ndx)
{
    std::vector<size_t> link_chain = std::move(m_link_chain);
    m_link_chain.clear();

    // Check if user-given template type equals Realm type. Todo, we should clean up and reuse all our
    // type traits (all the is_same() cases below).
    const Table* table = get_link_chain_target(link_chain);

    realm::DataType ct = table->get_column_type(column_ndx);
    if (std::is_same<T, int64_t>::value && ct != type_Int)
        throw(LogicError::type_mismatch);
    else if (std::is_same<T, bool>::value && ct != type_Bool)
        throw(LogicError::type_mismatch);
    else if (std::is_same<T, realm::OldDateTime>::value && ct != type_OldDateTime)
        throw(LogicError::type_mismatch);
    else if (std::is_same<T, float>::value && ct != type_Float)
        throw(LogicError::type_mismatch);
    else if (std::is_same<T, double>::value && ct != type_Double)
        throw(LogicError::type_mismatch);

    if (std::is_same<T, Link>::value || std::is_same<T, LinkList>::value || std::is_same<T, BackLink>::value) {
        link_chain.push_back(column_ndx);
    }

    return Columns<T>(column_ndx, this, std::move(link_chain));
}

template <class T>
inline Columns<T> Table::column(const Table& origin, size_t origin_col_ndx)
{
    static_assert(std::is_same<T, BackLink>::value, "");

    auto origin_table_key = origin.get_key();
    const Table& current_target_table = *get_link_chain_target(m_link_chain);
    size_t backlink_col_ndx = current_target_table.m_spec->find_backlink_column(origin_table_key, origin_col_ndx);

    std::vector<size_t> link_chain = std::move(m_link_chain);
    m_link_chain.clear();
    link_chain.push_back(backlink_col_ndx);

    return Columns<T>(backlink_col_ndx, this, std::move(link_chain));
}

template <class T>
SubQuery<T> Table::column(size_t column_ndx, Query subquery)
{
    static_assert(std::is_same<T, Link>::value, "A subquery must involve a link list or backlink column");
    return SubQuery<T>(column<T>(column_ndx), std::move(subquery));
}

template <class T>
SubQuery<T> Table::column(const Table& origin, size_t origin_col_ndx, Query subquery)
{
    static_assert(std::is_same<T, BackLink>::value, "A subquery must involve a link list or backlink column");
    return SubQuery<T>(column<T>(origin, origin_col_ndx), std::move(subquery));
}

// For use by queries
inline Table& Table::link(size_t link_column)
{
    m_link_chain.push_back(link_column);
    return *this;
}

inline Table& Table::backlink(const Table& origin, size_t origin_col_ndx)
{
    auto origin_table_key = origin.get_key();
    const Table& current_target_table = *get_link_chain_target(m_link_chain);
    size_t backlink_col_ndx = current_target_table.m_spec->find_backlink_column(origin_table_key, origin_col_ndx);
    return link(backlink_col_ndx);
}

inline bool Table::is_empty() const noexcept
{
    return m_size == 0;
}

inline size_t Table::size() const noexcept
{
    return m_size;
}


inline ConstTableRef Table::get_link_target(size_t col_ndx) const noexcept
{
    return const_cast<Table*>(this)->get_link_target(col_ndx);
}

inline ConstTableRef Table::get_parent_table(size_t* column_ndx_out) const noexcept
{
    return ConstTableRef(get_parent_table_ptr(column_ndx_out));
}

inline TableRef Table::get_parent_table(size_t* column_ndx_out) noexcept
{
    return TableRef(get_parent_table_ptr(column_ndx_out));
}

inline bool Table::is_group_level() const noexcept
{
    return bool(get_parent_group());
}

inline bool Table::operator==(const Table& t) const
{
    return *m_spec == *t.m_spec && compare_objects(t); // Throws
}

inline bool Table::operator!=(const Table& t) const
{
    return !(*this == t); // Throws
}

inline size_t Table::get_size_from_ref(ref_type top_ref, Allocator& alloc) noexcept
{
    const char* top_header = alloc.translate(top_ref);
    std::pair<int_least64_t, int_least64_t> p = Array::get_two(top_header, 0);
    ref_type spec_ref = to_ref(p.first), columns_ref = to_ref(p.second);
    return get_size_from_ref(spec_ref, columns_ref, alloc);
}

inline Table* Table::get_parent_table_ptr(size_t* column_ndx_out) noexcept
{
    const Table* parent = const_cast<const Table*>(this)->get_parent_table_ptr(column_ndx_out);
    return const_cast<Table*>(parent);
}

inline bool Table::is_link_type(ColumnType col_type) noexcept
{
    return col_type == col_type_Link || col_type == col_type_LinkList;
}

inline size_t* Table::record_subtable_path(size_t* b, size_t* e) const noexcept
{
    const Array& real_top = m_top.is_attached() ? m_top : m_columns;
    size_t index_in_parent = real_top.get_ndx_in_parent();
    REALM_ASSERT_3(b, <, e);
    *b++ = index_in_parent;
    ArrayParent* parent = real_top.get_parent();
    REALM_ASSERT(parent);
    REALM_ASSERT(dynamic_cast<Parent*>(parent));
    return static_cast<Parent*>(parent)->record_subtable_path(b, e);
}

inline size_t* Table::Parent::record_subtable_path(size_t* b, size_t*) noexcept
{
    return b;
}

inline bool Table::is_marked() const noexcept
{
    return m_mark;
}

inline void Table::mark() noexcept
{
    m_mark = true;
}

inline void Table::unmark() noexcept
{
    m_mark = false;
}

inline Replication* Table::get_repl() noexcept
{
    return m_top.get_alloc().get_replication();
}

inline void Table::set_ndx_in_parent(size_t ndx_in_parent) noexcept
{
    REALM_ASSERT(m_top.is_attached());
    m_top.set_ndx_in_parent(ndx_in_parent);
}


// This class groups together information about the target of a link column
// This is not a valid link if the target table == nullptr
struct LinkTargetInfo {
    LinkTargetInfo(Table* target = nullptr, size_t backlink_ndx = realm::npos)
        : m_target_table(target)
        , m_backlink_col_ndx(backlink_ndx)
    {
    }
    bool is_valid() const
    {
        return (m_target_table != nullptr);
    }
    Table* m_target_table;
    size_t m_backlink_col_ndx; // a value of npos indicates the backlink should be appended
};

// The purpose of this class is to give internal access to some, but
// not all of the non-public parts of the Table class.
class _impl::TableFriend {
public:
    typedef Table::UnbindGuard UnbindGuard;

    static ref_type create_empty_table(Allocator& alloc, TableKey key = TableKey())
    {
        return Table::create_empty_table(alloc, key); // Throws
    }

    static ref_type clone(const Table& table, Allocator& alloc)
    {
        return table.clone(alloc); // Throws
    }

    static ref_type clone_columns(const Table& table, Allocator& alloc)
    {
        return table.clone_columns(alloc); // Throws
    }

    static Table* create_accessor(Allocator& alloc, ref_type top_ref, Table::Parent* parent, size_t ndx_in_parent)
    {
        std::unique_ptr<Table> table(new Table(Table::ref_count_tag(), alloc)); // Throws
        table->init(top_ref, parent, ndx_in_parent);                            // Throws
        return table.release();
    }

    // Intended to be used only by Group::create_table_accessor()
    static Table* create_incomplete_accessor(Allocator& alloc, ref_type top_ref, Table::Parent* parent,
                                             size_t ndx_in_parent)
    {
        std::unique_ptr<Table> table(new Table(Table::ref_count_tag(), alloc)); // Throws
        bool skip_create_column_accessors = true;
        table->init(top_ref, parent, ndx_in_parent, skip_create_column_accessors); // Throws
        return table.release();
    }

    // Intended to be used only by Group::create_table_accessor()
    static void complete_accessor(Table& table)
    {
        table.refresh_column_accessors(); // Throws
    }

    static void set_top_parent(Table& table, ArrayParent* parent, size_t ndx_in_parent) noexcept
    {
        table.m_top.set_parent(parent, ndx_in_parent);
    }

    static void update_from_parent(Table& table, size_t old_baseline) noexcept
    {
        table.update_from_parent(old_baseline);
    }

    static void detach(Table& table) noexcept
    {
        table.detach();
    }

    static void discard_row_accessors(Table& table) noexcept
    {
        table.discard_row_accessors();
    }

    static void discard_child_accessors(Table& table) noexcept
    {
        table.discard_child_accessors();
    }

    static void bind_ptr(Table& table) noexcept
    {
        table.bind_ptr();
    }

    static void unbind_ptr(Table& table) noexcept
    {
        table.unbind_ptr();
    }

    static bool compare_objects(const Table& a, const Table& b)
    {
        return a.compare_objects(b); // Throws
    }

    static size_t get_size_from_ref(ref_type ref, Allocator& alloc) noexcept
    {
        return Table::get_size_from_ref(ref, alloc);
    }

    static size_t get_size_from_ref(ref_type spec_ref, ref_type columns_ref, Allocator& alloc) noexcept
    {
        return Table::get_size_from_ref(spec_ref, columns_ref, alloc);
    }

    static Spec& get_spec(Table& table) noexcept
    {
        return *table.m_spec;
    }

    static const Spec& get_spec(const Table& table) noexcept
    {
        return *table.m_spec;
    }

    static TableRef get_opposite_link_table(const Table& table, size_t col_ndx);

    static ColumnBase& get_column(const Table& table, size_t col_ndx)
    {
        return *table.m_cols[col_ndx];
    }

    static void do_remove_object(Table& table, Key key)
    {
        table.do_remove_object(key); // Throws
    }

    static void do_clear(Table& table)
    {
        bool broken_reciprocal_backlinks = false;
        table.do_clear(broken_reciprocal_backlinks); // Throws
    }

    static void do_set_link(Table& table, size_t col_ndx, size_t row_ndx, size_t target_row_ndx)
    {
        table.do_set_link(col_ndx, row_ndx, target_row_ndx); // Throws
    }

    static void remove_recursive(Table& table, CascadeState& rows)
    {
        table.remove_recursive(rows); // Throws
    }

    static size_t* record_subtable_path(const Table& table, size_t* b, size_t* e) noexcept
    {
        return table.record_subtable_path(b, e);
    }
    static void insert_column_unless_exists(Table& table, size_t column_ndx, DataType type, StringData name,
                                            LinkTargetInfo link, bool nullable = false, bool listtype = false,
                                            bool* was_inserted = nullptr)
    {
        table.do_insert_column_unless_exists(column_ndx, type, name, link, nullable, listtype,
                                             was_inserted); // Throws
    }

    static void erase_column(Table& table, size_t column_ndx)
    {
        table.remove_column(column_ndx); // Throws
    }

    static void rename_column(Table& table, size_t column_ndx, StringData name)
    {
        table.rename_column(column_ndx, name); // Throws
    }

    static void set_link_type(Table& table, size_t column_ndx, LinkType link_type)
    {
        table.set_link_type(column_ndx, link_type); // Throws
    }

    static void batch_erase_rows(Table& table, const KeyColumn& keys)
    {
        table.batch_erase_rows(keys); // Throws
    }

    static const Table* get_link_target_table_accessor(const Table& table, size_t col_ndx) noexcept
    {
        return const_cast<Table&>(table).get_link_target_table_accessor(col_ndx);
    }

    static Table* get_link_target_table_accessor(Table& table, size_t col_ndx) noexcept
    {
        return table.get_link_target_table_accessor(col_ndx);
    }


    static void adj_insert_column(Table& table, size_t col_ndx)
    {
        table.adj_insert_column(col_ndx); // Throws
    }

    static void adj_add_column(Table& table)
    {
        size_t num_cols = table.m_cols.size();
        table.adj_insert_column(num_cols); // Throws
    }

    static void adj_erase_column(Table& table, size_t col_ndx) noexcept
    {
        table.adj_erase_column(col_ndx);
    }

    static bool is_marked(const Table& table) noexcept
    {
        return table.is_marked();
    }

    static void mark(Table& table) noexcept
    {
        table.mark();
    }

    static void unmark(Table& table) noexcept
    {
        table.unmark();
    }

    static void recursive_mark(Table& table) noexcept
    {
        table.recursive_mark();
    }

    static void mark_link_target_tables(Table& table, size_t col_ndx_begin) noexcept
    {
        table.mark_link_target_tables(col_ndx_begin);
    }

    static void mark_opposite_link_tables(Table& table) noexcept
    {
        table.mark_opposite_link_tables();
    }

    typedef Table::AccessorUpdater AccessorUpdater;
    static void update_accessors(Table& table, AccessorUpdater& updater)
    {
        table.update_accessors(updater); // Throws
    }

    static void refresh_accessor_tree(Table& table)
    {
        table.refresh_accessor_tree(); // Throws
    }

    static void set_ndx_in_parent(Table& table, size_t ndx_in_parent) noexcept
    {
        table.set_ndx_in_parent(ndx_in_parent);
    }

    static bool is_link_type(ColumnType type) noexcept
    {
        return Table::is_link_type(type);
    }

    static void bump_version(Table& table, bool bump_global = true) noexcept
    {
        table.bump_version(bump_global);
    }

    static bool is_cross_table_link_target(const Table& table)
    {
        return table.is_cross_table_link_target();
    }

    static Group* get_parent_group(const Table& table) noexcept
    {
        return table.get_parent_group();
    }

    static Replication* get_repl(Table& table) noexcept
    {
        return table.get_repl();
    }
};


} // namespace realm

#endif // REALM_TABLE_HPP
