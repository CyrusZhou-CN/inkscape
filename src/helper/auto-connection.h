// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SEEN_AUTO_CONNECTION_H
#define SEEN_AUTO_CONNECTION_H

#include <utility>
#include <sigc++/connection.h>

namespace Inkscape {

// Class to simplify re-subscribing to connections; automates disconnecting
// TODO: GTK4: Migrate to sigc++ 3.6ʼs scoped_connection, which I wrote! —dboles

class auto_connection
{
public:
    auto_connection(sigc::connection const &c) noexcept
        : _connection(c)
    {}

    auto_connection() noexcept = default;
    ~auto_connection() { _connection.disconnect(); }

    // Disable copying, otherwise which copy should disconnect()?
    auto_connection(auto_connection const &) = delete;
    auto_connection &operator=(auto_connection const &) = delete;

    // re-assign
    auto_connection &operator=(sigc::connection const &c)
    {
        _connection.disconnect();
        _connection = c;
        return *this;
    }

    // Allow moving to support use in containers / transfer ‘ownership’
    auto_connection(auto_connection &&that) noexcept
        : _connection(std::exchange(that._connection, sigc::connection{}))
    {}
    auto_connection &operator=(auto_connection &&that)
    {
        _connection.disconnect();
        _connection = std::exchange(that._connection, sigc::connection{});
        return *this;
    }

    // Provide swap() for 2 instances, in which case neither need/can disconnect
    friend void swap(auto_connection &l, auto_connection &r) noexcept
    {
        using std::swap;
        swap(l._connection, r._connection);
    }

    /** Returns whether the connection is still active
     *  @returns @p true if connection is still ative
     */
    operator bool() const noexcept { return _connection.connected(); }

    /** Returns whether the connection is still active
     *  @returns @p true if connection is still ative
     */
    inline bool connected() const noexcept { return _connection.connected(); }

    /** Sets or unsets the blocking state of this connection.
     * @param should_block Indicates whether the blocking state should be set or unset.
     * @return @p true if the connection has been in blocking state before.
     */
    inline bool block(bool should_block = true) noexcept
    {
        return _connection.block(should_block);
    }

    inline bool unblock() noexcept { return _connection.unblock(); }

    void disconnect() { _connection.disconnect(); }

private:
    sigc::connection _connection;
};

} // namespace Inkscape

#endif // SEEN_AUTO_CONNECTION_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
