/*
 * Copyright (c) 2015, Peter Thorson. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the WebSocket++ Project nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL PETER THORSON BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef WEBSOCKETPP_COMMON_ASIO_HPP
#define WEBSOCKETPP_COMMON_ASIO_HPP

// This file goes to some length to preserve compatibility with versions of 
// boost older than 1.49 (where the first modern steady_timer timer based on 
// boost/std chrono was introduced.
//
// For the versions older than 1.49, the deadline_timer is used instead. this
// brings in dependencies on boost date_time and it has a different interface
// that is normalized by the `lib::asio::is_neg` and `lib::asio::milliseconds`
// wrappers provided by this file.
//
// The primary reason for this continued support is that boost 1.48 is the
// default and not easily changeable version of boost supplied by the package
// manager of popular Linux distributions like Ubuntu 12.04 LTS. Once the need
// for this has passed this should be cleaned up and simplified.

#ifdef ASIO_STANDALONE
    #include <asio/version.hpp>
    
    #if (ASIO_VERSION/100000) == 1 && ((ASIO_VERSION/100)%1000) < 8
        static_assert(false, "The minimum version of standalone Asio is 1.8.0");
    #endif
    
    #include <asio.hpp>
    #include <asio/steady_timer.hpp>
    #include <websocketpp/common/chrono.hpp> 
#else
    #include <boost/version.hpp>
    
    // See note above about boost <1.49 compatibility. If we are running on 
    // boost > 1.48 pull in the steady timer and chrono library
    #if (BOOST_VERSION/100000) == 1 && ((BOOST_VERSION/100)%1000) > 48
        #include <boost/asio/steady_timer.hpp>
        #include <websocketpp/common/chrono.hpp>
    #endif
    
    #include <boost/asio.hpp>
    #include <boost/system/error_code.hpp>
#endif

namespace websocketpp {
namespace lib {

#ifdef ASIO_STANDALONE
    namespace asio {
        using namespace ::asio;
        // Here we assume that we will be using std::error_code with standalone
        // Asio. This is probably a good assumption, but it is possible in rare
        // cases that local Asio versions would be used.
        using std::errc;
        
        // See note above about boost <1.49 compatibility. Because we require
        // a standalone Asio version of 1.8+ we are guaranteed to have 
        // steady_timer available. By convention we require the chrono library
        // (either boost or std) for use with standalone Asio.
        template <typename T>
        bool is_neg(T duration) {
            return duration.count() < 0;
        }
        inline lib::chrono::milliseconds milliseconds(long duration) {
            return lib::chrono::milliseconds(duration);
        }
    } // namespace asio
    
#else
    namespace asio {
        using namespace boost::asio;

        // Boost 1.66+ API compatibility layer for WebSocketPP
        // WebSocketPP was written for Boost.Asio pre-1.66 and needs these wrappers
        #if (BOOST_VERSION/100000) == 1 && ((BOOST_VERSION/100)%1000) >= 66
            // 1. io_service was renamed to io_context
            class io_service : public boost::asio::io_context {
            public:
                // Inherit constructors
                using boost::asio::io_context::io_context;

                // Add compatibility methods that were moved to free functions
                template<typename CompletionHandler>
                void post(CompletionHandler&& handler) {
                    boost::asio::post(*this, std::forward<CompletionHandler>(handler));
                }

                // reset() was renamed to restart()
                void reset() {
                    this->restart();
                }

                // Strand wrapper for compatibility
                class strand {
                private:
                    boost::asio::strand<boost::asio::io_context::executor_type> impl_;
                public:
                    explicit strand(io_service& ios) : impl_(boost::asio::make_strand(ios)) {}

                    template<typename Handler>
                    auto wrap(Handler&& h) {
                        return boost::asio::bind_executor(impl_, std::forward<Handler>(h));
                    }
                };

                // work was renamed to executor_work_guard
                class work {
                private:
                    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> impl_;
                public:
                    explicit work(io_service& ios) : impl_(boost::asio::make_work_guard(ios)) {}
                };
            };

            // 2. Timer expires_from_now() compatibility
            // Wrap steady_timer to add back the expires_from_now() method
            class steady_timer : public boost::asio::steady_timer {
            public:
                using boost::asio::steady_timer::steady_timer;

                // Add back expires_from_now() method
                duration expires_from_now() const {
                    return this->expiry() - clock_type::now();
                }
            };

            // 3. Resolver compatibility
            namespace ip {
                namespace tcp {
                    // Expose standard types that don't need wrapping
                    typedef boost::asio::ip::tcp::socket socket;
                    typedef boost::asio::ip::tcp::endpoint endpoint;
                    typedef boost::asio::ip::tcp::acceptor acceptor;

                    // v4 and v6 are functions in modern Boost.ASIO, not types
                    // We need to forward them as inline functions
                    inline boost::asio::ip::tcp v4() { return boost::asio::ip::tcp::v4(); }
                    inline boost::asio::ip::tcp v6() { return boost::asio::ip::tcp::v6(); }

                    class resolver : public boost::asio::ip::tcp::resolver {
                    public:
                        using boost::asio::ip::tcp::resolver::resolver;

                        // query class for old API
                        class query {
                        private:
                            std::string host_;
                            std::string service_;
                        public:
                            query(const std::string& host, const std::string& service)
                                : host_(host), service_(service) {}
                            const std::string& host_name() const { return host_; }
                            const std::string& service_name() const { return service_; }
                        };

                        // Iterator wrapper to emulate old resolver::iterator API
                        // Old API allowed default-constructed "end" iterators, but new results_type doesn't
                        class iterator {
                        private:
                            typedef boost::asio::ip::tcp::resolver::results_type results_type;
                            typedef results_type::iterator results_iterator;

                            // Store results and current position
                            // Use shared_ptr to allow copying while maintaining results lifetime
                            std::shared_ptr<results_type> results_;
                            results_iterator current_;
                            bool is_end_;

                        public:
                            // Use basic_resolver_entry as value_type (matches new Boost API)
                            // This type has .endpoint() method and implicit conversion to endpoint
                            typedef typename results_type::value_type value_type;
                            typedef const value_type& reference;
                            typedef const value_type* pointer;
                            typedef std::ptrdiff_t difference_type;
                            typedef std::forward_iterator_tag iterator_category;

                            // Default constructor creates an "end" iterator
                            iterator() : results_(), current_(), is_end_(true) {}

                            // Construct from results (begin iterator)
                            explicit iterator(results_type&& results)
                                : results_(std::make_shared<results_type>(std::move(results)))
                                , current_(results_->begin())
                                , is_end_(current_ == results_->end()) {}

                            // Copy constructor
                            iterator(const iterator& other) = default;
                            iterator& operator=(const iterator& other) = default;

                            // Dereference operators - return basic_resolver_entry
                            reference operator*() const { return *current_; }
                            pointer operator->() const { return current_.operator->(); }

                            // Pre-increment
                            iterator& operator++() {
                                if (!is_end_) {
                                    ++current_;
                                    if (current_ == results_->end()) {
                                        is_end_ = true;
                                    }
                                }
                                return *this;
                            }

                            // Post-increment
                            iterator operator++(int) {
                                iterator tmp(*this);
                                ++(*this);
                                return tmp;
                            }

                            // Equality comparison
                            bool operator==(const iterator& other) const {
                                // Both are end iterators
                                if (is_end_ && other.is_end_) return true;
                                // One is end, other is not
                                if (is_end_ != other.is_end_) return false;
                                // Both are non-end iterators from same results
                                if (results_ == other.results_) {
                                    return current_ == other.current_;
                                }
                                // Different results objects - not equal
                                return false;
                            }

                            bool operator!=(const iterator& other) const {
                                return !(*this == other);
                            }
                        };

                        // resolve() method with old query API - returns our wrapper iterator
                        iterator resolve(const query& q) {
                            return iterator(boost::asio::ip::tcp::resolver::resolve(q.host_name(), q.service_name()));
                        }

                        // async_resolve() with old API - handler receives our wrapper iterator
                        template<typename ResolveHandler>
                        void async_resolve(const query& q, ResolveHandler&& handler) {
                            boost::asio::ip::tcp::resolver::async_resolve(
                                q.host_name(), q.service_name(),
                                [handler = std::forward<ResolveHandler>(handler)](
                                    const boost::system::error_code& ec,
                                    boost::asio::ip::tcp::resolver::results_type results) mutable {
                                    // Convert results_type to our iterator wrapper
                                    iterator it = ec ? iterator() : iterator(std::move(results));
                                    handler(ec, it);
                                });
                        }
                    };
                }
            }

            // 4. socket_base::max_connections compatibility
            namespace socket_base {
                constexpr int max_connections = boost::asio::socket_base::max_listen_connections;
                typedef boost::asio::socket_base::reuse_address reuse_address;
            }
        #endif

        // See note above about boost <1.49 compatibility
        #if (BOOST_VERSION/100000) == 1 && ((BOOST_VERSION/100)%1000) > 48
            // Using boost::asio >=1.49 so we use chrono and steady_timer
            template <typename T>
            bool is_neg(T duration) {
                return duration.count() < 0;
            }

            // If boost believes it has std::chrono available it will use it
            // so we should also use it for things that relate to boost, even
            // if the library would otherwise use boost::chrono.
            #if defined(BOOST_ASIO_HAS_STD_CHRONO)
                inline std::chrono::milliseconds milliseconds(long duration) {
                    return std::chrono::milliseconds(duration);
                }
            #else
                inline lib::chrono::milliseconds milliseconds(long duration) {
                    return lib::chrono::milliseconds(duration);
                }
            #endif
        #else
            // Using boost::asio <1.49 we pretend a deadline timer is a steady
            // timer and wrap the negative detection and duration conversion
            // appropriately.
            typedef boost::asio::deadline_timer steady_timer;
            
            template <typename T>
            bool is_neg(T duration) {
                return duration.is_negative();
            }
            inline boost::posix_time::time_duration milliseconds(long duration) {
                return boost::posix_time::milliseconds(duration);
            }
        #endif
        
        using boost::system::error_code;
        namespace errc = boost::system::errc;
    } // namespace asio
#endif


} // namespace lib
} // namespace websocketpp

#endif // WEBSOCKETPP_COMMON_ASIO_HPP
