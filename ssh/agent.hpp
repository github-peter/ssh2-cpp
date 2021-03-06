/**
    @file

    Key-agent protocol.

    @if license

    Copyright (C) 2012, 2013  Alexander Lamaison <awl03@doc.ic.ac.uk>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

    In addition, as a special exception, the the copyright holders give you
    permission to combine this program with free software programs or the 
    OpenSSL project's "OpenSSL" library (or with modified versions of it, 
    with unchanged license). You may copy and distribute such a system 
    following the terms of the GNU GPL for this program and the licenses 
    of the other code concerned. The GNU General Public License gives 
    permission to release a modified version without this exception; this 
    exception also makes it possible to release a modified version which 
    carries forward this exception.

    @endif
*/

#ifndef SSH_AGENT_HPP
#define SSH_AGENT_HPP

#include <ssh/detail/agent_state.hpp>
#include <ssh/detail/session_state.hpp>
#include <ssh/detail/libssh2/agent.hpp> // ssh::detail::libssh2::agent

#include <boost/iterator/iterator_facade.hpp>
#include <boost/make_shared.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/throw_exception.hpp> // BOOST_THROW_EXCEPTION

#include <string>

#include <libssh2.h> // LIBSSH2_AGENT, libssh2_agent_*

namespace ssh {

class identity
{
public:

    identity(
        boost::shared_ptr<detail::agent_state> agent,
        libssh2_agent_publickey* identity) :
    m_agent(agent), m_identity(identity) {}

    void authenticate(const std::string& user_name)
    {
        detail::agent_state::scoped_lock lock = m_agent->aquire_lock();

        detail::libssh2::agent::userauth(
            m_agent->agent_ptr(), m_agent->session_ptr(), user_name.c_str(),
            m_identity);
    }
        
private:

    boost::shared_ptr<detail::agent_state> m_agent;
    libssh2_agent_publickey* m_identity;
};

namespace detail {

template<typename IdentityType>
class identity_iterator_base :
    public boost::iterator_facade<
        identity_iterator_base<IdentityType>,
        IdentityType,
        boost::forward_traversal_tag, // this tag allows value return
        IdentityType>
{
    friend class boost::iterator_core_access;

    // Enables conversion constructor to work:
    template<typename> friend class identity_iterator_base;

public:

    identity_iterator_base(boost::shared_ptr<agent_state> agent)
        : m_agent(agent), m_pos(NULL)
    {
        increment();
    }

    /**
     * End iterator.
     */
    identity_iterator_base()  : m_pos(NULL) {}

    /**
     * Copy conversion constructor.
     *
     * Purpose: to allow mutable iterators to be converted to const iterators.
     */
    template<typename OtherValue>
    identity_iterator_base(const identity_iterator_base<OtherValue>& other)
        : m_agent(other.m_agent), m_pos(other.m_pos) {}

private:

    void increment()
    {
        if (!m_agent)
            BOOST_THROW_EXCEPTION(
                std::logic_error(
                    "Can't increment past the end of a collection"));

        detail::agent_state::scoped_lock lock = m_agent->aquire_lock();

        bool no_more_identities = detail::libssh2::agent::get_identity(
            m_agent->agent_ptr(), m_agent->session_ptr(), &m_pos, m_pos) == 1;

        if (no_more_identities)
        {
            // Use m_agent as the end marker as a NULL m_pos means start again
            m_agent.reset();
            m_pos = NULL; // To keep equality with the end iterator happy
        }
    }

    bool equal(const identity_iterator_base& other) const
    {
        return m_agent == other.m_agent && m_pos == other.m_pos;
    }

    value_type dereference() const
    {
        if (!m_agent)
            BOOST_THROW_EXCEPTION(
                std::logic_error("Can't dereference the end of a collection"));

        return identity(m_agent, m_pos);
    }

    boost::shared_ptr<agent_state> m_agent;
    libssh2_agent_publickey* m_pos;
};

}

/**
 * A connection to an SSH key agent.
 *
 * When this object is created, all the identities currently stored in it are
 * copied out.  If you need a fresh list, request a new agent instance.
 */
class agent_identities
{
public:

    typedef detail::identity_iterator_base<identity> iterator;
    typedef detail::identity_iterator_base<const identity> const_iterator;

    explicit agent_identities(detail::session_state& session)
        :
    m_agent(
        boost::make_shared<detail::agent_state>(boost::ref(session)))
        // http://stackoverflow.com/a/1374266/67013
    {
        // We pull the identities out here (AND ONLY HERE) so that all copies
        // of the agent, iterators and identity objects refer to valid data.
        // If we called this when creating the iterator it would wipe out all
        // other iterators.

        detail::agent_state::scoped_lock lock = m_agent->aquire_lock();

        ::ssh::detail::libssh2::agent::list_identities(
            m_agent->agent_ptr(), m_agent->session_ptr());
    }

    iterator begin() const
    {
        return iterator(m_agent);
    }

    iterator end() const
    {
        return iterator();
    }

private:

    boost::shared_ptr<detail::agent_state> m_agent;
};

} // namespace ssh

#endif
