# Migrating to Nova from Other Logging Frameworks

This guide provides step-by-step instructions for migrating from popular logging frameworks to Nova, including strategies for gradual migration and coexistence.

## Table of Contents

1. [Wrapper/Facade Migration](#wrapperfacade-migration)
2. [Migration Strategies](#migration-strategies)
3. [From spdlog](#from-spdlog)
4. [From glog](#from-glog)
5. [From Boost.Log](#from-boostlog)
6. [From log4cplus](#from-log4cplus)
7. [From easylogging++](#from-easylogging)
8. [Adapter Pattern for Gradual Migration](#adapter-pattern-for-gradual-migration)
9. [Common Patterns](#common-patterns)
10. [Troubleshooting](#troubleshooting)

---

## Wrapper/Facade Migration

If your codebase already has a logging wrapper or facade in place, the fastest migration path is to swap Nova in as the backend while leaving all call sites untouched.  Only the wrapper implementation changes:

```cpp
#include <kmac/nova.h>
#include <kmac/nova/extras/severities.h>

using namespace kmac::nova::extras;

namespace AppLog
{
    // severity-based - matches existing call sites
    inline void info( const char* msg ) { NOVA_LOG( InfoTag ) << msg; }
    inline void error( const char* msg ) { NOVA_LOG( ErrorTag ) << msg; }

    // domain-based - compile-time routing, per-domain sink binding preserved
    template< typename Tag >
    inline void log( const char* msg ) { NOVA_LOG( Tag ) << msg; }
}
```

**What is preserved**: compile-time routing and per-domain sink binding are fully available when using the templated form.

**What is lost**: zero-cost disabled logging - the wrapper call exists at the call site regardless of whether the domain is enabled, so argument evaluation and call overhead remain even for disabled tags.

If no existing wrapper is in place, adding one costs roughly the same effort as direct replacement but gives up zero-cost disabling - making direct replacement the better choice.  See the Migration Strategies section below for the direct replacement approach.

---

## Migration Strategies

### Strategy 1: Complete Replacement

Best for: new projects or small codebases

1. remove old logging library dependencies
2. add Nova headers
3. define domains (tags) for your subsystems/severities
4. configure sinks
5. replace log calls with Nova macros

---

### Strategy 2: Gradual Migration (Adapter Pattern)

Best for: large codebases, production systems, risk-averse environments

Use Nova sinks that **adapt** to your existing logging framework, allowing both systems to coexist:

```cpp
// Nova sink that forwards to spdlog
class SpdlogAdapterSink : public kmac::nova::Sink {
private:
	std::shared_ptr< spdlog::logger > _logger;
public:
	void process( const kmac::nova::Record& record ) noexcept override {
		_logger->info( "[{}] {}", record.tag, record.message );
	}
};
```

This allows you to:
- migrate module-by-module
- keep existing logs working
- test Nova incrementally
- maintain a single log output

---

### Strategy 3: Side-by-Side (Parallel Operation)

Best for: critical systems requiring extensive testing

Run both logging systems simultaneously:
- old system: existing production logs
- Nova: new subsystem logs or enhanced diagnostics

Gradually shift functionality from old to new.

---

## From spdlog

### Conceptual Mapping

| spdlog Concept | Nova Equivalent |
|----------------|-----------------|
| `logger->info()` | `NOVA_LOG(InfoTag)` |
| `logger->debug()` | `NOVA_LOG(DebugTag)` |
| `spdlog::logger` | `Logger<Tag>` |
| `spdlog::sink` | `nova::Sink` |
| Registry | No equivalent (explicit binding) |
| Formatting pattern | `FormattingSink` |

### Migration Steps

**Before (spdlog):**
```cpp
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

auto logger = spdlog::stdout_color_mt( "applogger" );
logger->set_level( spdlog::level::debug );

logger->info( "Server started on port {}", 8080 );
logger->debug( "Processing request from {}", clientIP );
logger->error( "Failed to connect: {}", errorMsg );
```

**After (Nova):**
```cpp
#include <kmac/nova.h>
#include <kmac/nova/extras/ostream_sink.h>
#include <kmac/nova/extras/severities.h>    // pre-defined severity tags

using namespace kmac::nova::extras;

// configure sinks
OStreamSink console( std::cout );
kmac::nova::ScopedConfigurator config;
config.bind< InfoTag >( &console );
config.bind< DebugTag >( &console );
config.bind< ErrorTag >( &console );

// log messages
NOVA_LOG_INFO() << "Server started on port " << 8080;
NOVA_LOG_DEBUG() << "Processing request from " << clientIP;
NOVA_LOG_ERROR() << "Failed to connect: " << errorMsg;
```

### Adapter Sink for Gradual Migration

```cpp
#include <spdlog/spdlog.h>
#include <kmac/nova/sink.h>  // or <kmac/nova.h>

class SpdlogAdapterSink : public kmac::nova::Sink
{
private:
	std::shared_ptr< spdlog::logger > _logger;
    
public:
	explicit SpdlogAdapterSink( std::shared_ptr< spdlog::logger > logger )
		: _logger( std::move( logger ) )
	{
	}

	void process( const kmac::nova::Record& record ) noexcept override
	{
		using namespace kmac::nova::extras;
		// spdlog can throw by default; try-catch is not needed if SPDLOG_NO_EXCEPTIONS is defined
		try {
			switch ( record.tagId )
			{
				case kmac::nova::LoggerTraits< ErrorTag >::tagId:
				case kmac::nova::LoggerTraits< FatalTag >::tagId:
					_logger->error( "[{}] {}:{} {}",
						record.tag, record.file, record.line, record.message );
					break;
				case kmac::nova::LoggerTraits< WarningTag >::tagId:
					_logger->warn( "[{}] {}:{} {}",
						record.tag, record.file, record.line, record.message );
					break;
				case kmac::nova::LoggerTraits< DebugTag >::tagId:
				case kmac::nova::LoggerTraits< TraceTag >::tagId:
					_logger->debug( "[{}] {}:{} {}",
						record.tag, record.file, record.line, record.message );
					break;
				default:
					_logger->info( "[{}] {}:{} {}",
						record.tag, record.file, record.line, record.message );
					break;
			}
		}
		catch ( ... ) {
			// silently ignore exception to maintain noexcept contract
		}
	}
};

// usage:
auto spdlogLogger = spdlog::stdout_color_mt( "app" );
SpdlogAdapterSink adapter( spdlogLogger );

kmac::nova::ScopedConfigurator config;
config.bind< InfoTag >( &adapter );  // Nova logs -> spdlog
```

Now you can migrate one module at a time while maintaining all logs in spdlog's output.

---

## From glog

### Conceptual Mapping

| glog Concept | Nova Equivalent |
|--------------|-----------------|
| `LOG(INFO)` | `NOVA_LOG(InfoTag)` |
| `LOG(WARNING)` | `NOVA_LOG(WarningTag)` |
| `VLOG(level)` | Custom verbosity tags |
| `CHECK()` | Use NOVA_ASSERT or separate assertion library |
| `--v=N` flag | Compile-time enabled/disabled tags |

### Migration Steps

**Before (glog):**
```cpp
#include <glog/logging.h>

google::InitGoogleLogging( argv[ 0 ] );
FLAGS_logtostderr = 1;

LOG( INFO ) << "Server starting";
LOG( WARNING ) << "Low memory: " << freeMemory << " bytes";
LOG( ERROR ) << "Connection failed: " << error;

VLOG( 1 ) << "Detailed trace info";
```

**After (Nova):**
```cpp
#include <kmac/nova.h>
#include <kmac/nova/extras/ostream_sink.h>
#include <kmac/nova/extras/severities.h>    // pre-defined severity tags

using namespace kmac::nova::extras;

// configure sinks
OStreamSink console( std::cerr );  // glog defaults to stderr
kmac::nova::ScopedConfigurator config;
config.bind< InfoTag >( &console );
config.bind< WarningTag >( &console );
config.bind< ErrorTag >( &console );

// log messages
NOVA_LOG_INFO() << "Server starting";
NOVA_LOG_WARN() << "Low memory: " << freeMemory << " bytes";
NOVA_LOG_ERROR() << "Connection failed: " << error;

// for VLOG - define custom verbosity tags
struct VerboseTag {};
NOVA_LOGGER_TRAITS( VerboseTag, VERBOSE, true, /* timestamp */ );
NOVA_LOG( VerboseTag ) << "Detailed trace info";
```

### Adapter Sink for Gradual Migration

```cpp
#include <glog/logging.h>
#include <kmac/nova/sink.h>  // or <kmac/nova.h>

class GlogAdapterSink : public kmac::nova::Sink
{
public:
	void process( const kmac::nova::Record& record ) noexcept override
	{
		using namespace kmac::nova::extras;
		// glog LOG() macros do not throw
		switch ( record.tagId )
		{
			case kmac::nova::LoggerTraits< ErrorTag >::tagId:
			case kmac::nova::LoggerTraits< FatalTag >::tagId:
				LOG( ERROR ) << "[" << record.tag << "] " << record.message;
				break;
			case kmac::nova::LoggerTraits< WarningTag >::tagId:
				LOG( WARNING ) << "[" << record.tag << "] " << record.message;
				break;
			default:
				LOG( INFO ) << "[" << record.tag << "] " << record.message;
				break;
		}
	}
};
```

---

## From Boost.Log

### Conceptual Mapping

| Boost.Log Concept | Nova Equivalent |
|-------------------|-----------------|
| `BOOST_LOG_TRIVIAL(info)` | `NOVA_LOG(InfoTag)` |
| Source/Sink | Tag/Sink |
| Attribute | LoggerTraits |
| Filter | FilterSink |
| Channel | Tag (any type) |

### Migration Steps

**Before (Boost.Log):**
```cpp
#include <boost/log/trivial.hpp>

BOOST_LOG_TRIVIAL( trace ) << "Entering function";
BOOST_LOG_TRIVIAL( info ) << "Server started";
BOOST_LOG_TRIVIAL( error ) << "Connection lost";
```

**After (Nova):**
```cpp
#include <kmac/nova/macros.h>
#include <kmac/nova/scoped_configurator.h>
#include <kmac/nova/extras/ostream_sink.h>
#include <kmac/nova/extras/severities.h>    // pre-defined severity tags

using namespace kmac::nova::extras;

// configure sinks
OStreamSink console( std::cout );
kmac::nova::ScopedConfigurator config;
config.bind< TraceTag >( &console );
config.bind< InfoTag >( &console );
config.bind< ErrorTag >( &console );

// log messages
NOVA_LOG_TRACE() << "Entering function";
NOVA_LOG_INFO() << "Server started";
NOVA_LOG_ERROR() << "Connection lost";
```

### Adapter Sink

```cpp
#include <boost/log/trivial.hpp>
#include <kmac/nova/sink.h>  // or <kmac/nova.h>

class BoostLogAdapterSink : public kmac::nova::Sink
{
public:
	void process( const kmac::nova::Record& record ) noexcept override
	{
		using namespace kmac::nova::extras;
		// Boost.Log can throw if a backend sink encounters an error
		try {
			switch ( record.tagId )
			{
				case kmac::nova::LoggerTraits< TraceTag >::tagId:
					BOOST_LOG_TRIVIAL( trace ) << "[" << record.tag << "] " << record.message;
					break;
				case kmac::nova::LoggerTraits< DebugTag >::tagId:
					BOOST_LOG_TRIVIAL( debug ) << "[" << record.tag << "] " << record.message;
					break;
				case kmac::nova::LoggerTraits< WarningTag >::tagId:
					BOOST_LOG_TRIVIAL( warning ) << "[" << record.tag << "] " << record.message;
					break;
				case kmac::nova::LoggerTraits< ErrorTag >::tagId:
					BOOST_LOG_TRIVIAL( error ) << "[" << record.tag << "] " << record.message;
					break;
				case kmac::nova::LoggerTraits< FatalTag >::tagId:
					BOOST_LOG_TRIVIAL( fatal ) << "[" << record.tag << "] " << record.message;
					break;
				default:
					BOOST_LOG_TRIVIAL( info ) << "[" << record.tag << "] " << record.message;
					break;
			}
		}
		catch ( ... ) {
			// silently ignore exception to maintain noexcept contract
		}
	}
};
```

---

## From log4cplus

### Conceptual Mapping

| log4cplus Concept | Nova Equivalent |
|-------------------|-----------------|
| `Logger::getInstance("name")` | `Logger<Tag>` |
| `LOG4CPLUS_INFO(logger, msg)` | `NOVA_LOG(InfoTag)` |
| Appender | Sink |
| Layout | FormattingSink |
| Configuration file | Explicit C++ code |

> NOTE: log4cplus, log4cpp, and log4cxx all share the same log4j concepts (hierarchical loggers, appenders, layouts) but differ in API details and maintenance status. Users migrating from log4cpp or log4cxx will find the concepts familiar - the main differences are the macro prefix (LOG4CPLUS_* vs LOG4CXX_*) and constructor style.  See docs/LIBRARY_COMPARISON.md for a comparison of maintenance status and license terms.

### Migration Steps

**Before (log4cplus):**
```cpp
#include <log4cplus/logger.h>
#include <log4cplus/loggingmacros.h>
#include <log4cplus/initializer.h>
#include <log4cplus/consoleappender.h>

log4cplus::Initializer initializer;
log4cplus::Logger logger = log4cplus::Logger::getInstance( "App.Network" );

LOG4CPLUS_INFO( logger, "Connection established" );
LOG4CPLUS_DEBUG( logger, "Bytes received: " << count );
LOG4CPLUS_ERROR( logger, "Timeout occurred" );
```

**After (Nova):**
```cpp
#include <kmac/nova.h>
#include <kmac/nova/extras/hierarchical_tag.h>
#include <kmac/nova/extras/severities.h>
#include <kmac/nova/extras/ostream_sink.h>

using namespace kmac::nova::extras;

// define subsystem
struct NetworkSubsystem {};

// use hierarchical tags for subsystem + severity
using NetworkInfo = HierarchicalTag< NetworkSubsystem, InfoTag >;
using NetworkDebug = HierarchicalTag< NetworkSubsystem, DebugTag >;
using NetworkError = HierarchicalTag< NetworkSubsystem, ErrorTag >;

// configure sinks
OStreamSink console( std::cout );
kmac::nova::ScopedConfigurator config;
config.bind< NetworkInfo >( &console );
config.bind< NetworkDebug >( &console );
config.bind< NetworkError >( &console );

// log
NOVA_LOG( NetworkInfo ) << "Connection established";
NOVA_LOG( NetworkDebug ) << "Bytes received: " << count;
NOVA_LOG( NetworkError ) << "Timeout occurred";
```

### Adapter Sink

```cpp
#include <kmac/nova/sink.h>  // or <kmac/nova.h>
#include <log4cplus/logger.h>
#include <log4cplus/loggingmacros.h>

class Log4cplusAdapterSink : public kmac::nova::Sink
{
private:
	log4cplus::Logger _logger;

public:
	explicit Log4cplusAdapterSink( const std::string& loggerName )
		: _logger( log4cplus::Logger::getInstance( loggerName ) )
	{}

	void process( const kmac::nova::Record& record ) noexcept override
	{
		using namespace kmac::nova::extras;
		// log4cplus exception behaviour is not explicitly guaranteed by the logging macros
		try {
			switch ( record.tagId )
			{
				case kmac::nova::LoggerTraits< ErrorTag >::tagId:
				case kmac::nova::LoggerTraits< FatalTag >::tagId:
					LOG4CPLUS_ERROR( _logger, record.message );
					break;
				case kmac::nova::LoggerTraits< WarningTag >::tagId:
					LOG4CPLUS_WARN( _logger, record.message );
					break;
				case kmac::nova::LoggerTraits< DebugTag >::tagId:
				case kmac::nova::LoggerTraits< TraceTag >::tagId:
					LOG4CPLUS_DEBUG( _logger, record.message );
					break;
				default:
					LOG4CPLUS_INFO( _logger, record.message );
					break;
			}
			// NOTE: this adapter handles Nova's built-in severity tags.
			// HierarchicalTag combinations (e.g. HierarchicalTag<NetworkSubsystem, ErrorTag>)
			// are routed via the default case - add explicit cases for known HierarchicalTag
			// tagIds if finer-grained routing is needed.
		}
		catch ( ... ) {
			// silently ignore exception to maintain noexcept contract
		}
	}
};
```

---

## From Easylogging++

> ⚠️ Easylogging++ has been archived and is no longer maintained.  Migration to Nova (or another active library) is recommended.

### Conceptual Mapping

| Easylogging++ Concept | Nova Equivalent |
|-----------------------|-----------------|
| `LOG(INFO)` | `NOVA_LOG(InfoTag)` |
| `VLOG(level)` | Custom verbosity tags |
| Configuration file | Explicit C++ code |
| Logger ID | Tag type |

### Migration Steps

**Before (Easylogging++):**
```cpp
#include <easylogging++.h>

INITIALIZE_EASYLOGGINGPP

LOG( INFO ) << "Application started";
LOG( DEBUG ) << "Processing item: " << itemId;
LOG( ERROR ) << "Database error: " << err;

VLOG(1) << "Verbose trace";
```

**After (Nova):**
```cpp
#include <kmac/nova.h>
#include <kmac/nova/extras/ostream_sink.h>
#include <kmac/nova/extras/severities.h>

using namespace kmac::nova::extras;

// configure sinks
OStreamSink console( std::cout );
kmac::nova::ScopedConfigurator config;
config.bind< DebugTag >( &console );
config.bind< InfoTag >( &console );
config.bind< ErrorTag >( &console );

// log messages
NOVA_LOG_INFO() << "Application started";
NOVA_LOG_DEBUG() << "Processing item: " << itemId;
NOVA_LOG_ERROR() << "Database error: " << err;

// define custom verbosity tag
struct VerboseTag {};
NOVA_LOGGER_TRAITS( VerboseTag, VERBOSE, true, kmac::nova::TimestampHelper::steadyNanosecs );
config.bind< VerboseTag >( &console );
NOVA_LOG( VerboseTag ) << "Verbose trace";
```

### Adapter Sink

```cpp
#include <easylogging++.h>
#include <kmac/nova/sink.h>  // or <kmac/nova.h>

class EasyloggingAdapterSink : public kmac::nova::Sink
{
public:
	void process( const kmac::nova::Record& record ) noexcept override
	{
		using namespace kmac::nova::extras;
		// Easylogging++ is archived; exception behaviour is uncertain
		try {
			switch ( record.tagId )
			{
				case kmac::nova::LoggerTraits< TraceTag >::tagId:
					VLOG( 9 ) << "[" << record.tag << "] " << record.message;
					break;
				case kmac::nova::LoggerTraits< DebugTag >::tagId:
					LOG( DEBUG ) << "[" << record.tag << "] " << record.message;
					break;
				case kmac::nova::LoggerTraits< WarningTag >::tagId:
					LOG( WARNING ) << "[" << record.tag << "] " << record.message;
					break;
				case kmac::nova::LoggerTraits< ErrorTag >::tagId:
					LOG( ERROR ) << "[" << record.tag << "] " << record.message;
					break;
				case kmac::nova::LoggerTraits< FatalTag >::tagId:
					LOG( FATAL ) << "[" << record.tag << "] " << record.message;
					break;
				default:
					LOG( INFO ) << "[" << record.tag << "] " << record.message;
					break;
			}
		}
		catch ( ... ) {
			// silently ignore exception
		}
	}
};
```

---

## Adapter Pattern for Gradual Migration

The **Adapter Pattern** (also called **Wrapper Pattern**) is the key to gradual migration.  A Nova sink acts as an adapter, translating Nova's logging interface into calls to your existing logging framework.

### Benefits

1. **Incremental migration** - convert one module at a time
2. **Unified output** - all logs go through existing system
3. **Low risk** - both systems can coexist
4. **Reversible** - easy to roll back if needed
5. **Testing** - verify Nova behavior before full commitment

### Generic Adapter Template

```cpp
#include <kmac/nova/sink.h>  // or <kmac/nova.h>

template< typename LegacyLogger >
class LegacyLoggerAdapter : public kmac::nova::Sink
{
private:
	LegacyLogger& _legacyLogger;

public:
	explicit LegacyLoggerAdapter( LegacyLogger& logger )
		: _legacyLogger( logger )
	{}

	void process( const kmac::nova::Record& record ) noexcept override
	{
		using namespace kmac::nova::extras;
		// exception behaviour of the legacy logger is unknown
		try {
			// format message
			std::string formatted = formatMessage( record );

			// use tagId switch for efficient, type-safe severity routing
			switch ( record.tagId )
			{
				case kmac::nova::LoggerTraits< ErrorTag >::tagId:
				case kmac::nova::LoggerTraits< FatalTag >::tagId:
					_legacyLogger.error( formatted );
					break;
				case kmac::nova::LoggerTraits< WarningTag >::tagId:
					_legacyLogger.warn( formatted );
					break;
				case kmac::nova::LoggerTraits< DebugTag >::tagId:
				case kmac::nova::LoggerTraits< TraceTag >::tagId:
					_legacyLogger.debug( formatted );
					break;
				default:
					_legacyLogger.info( formatted );
					break;
			}
		}
		catch ( ... ) {
			// silently ignore exception to maintain noexcept contract
		}
	}

private:
	std::string formatMessage( const kmac::nova::Record& record )
	{
		std::ostringstream oss;
		oss << "[" << record.tag << "] "
			<< record.file << ":" << record.line << " "
			<< std::string( record.message, record.messageSize );
		return oss.str();
	}
};
```

### Migration Workflow

**Phase 1: Setup Adapter**
```cpp
// existing logger still active
auto legacyLogger = /* your existing logger */;

// create Nova adapter
LegacyLoggerAdapter adapter( legacyLogger );

// configure Nova to use adapter
kmac::nova::ScopedConfigurator config;
config.bind< InfoTag >( &adapter );
config.bind< ErrorTag >( &adapter );
// ... bind other tags
```

**Phase 2: Migrate Module by Module**
```cpp
// old code (unchanged)
legacyLogger.info( "Module A: Starting" );

// new code in Module B (Nova)
NOVA_LOG( InfoTag ) << "Module B: Starting";

// both outputs go through legacy logger's sink
```

**Phase 3: Remove Adapter**

Once all modules use Nova:
```cpp
// replace adapter with native Nova sinks
OStreamSink console( std::cout );
config.bind< InfoTag >( &console );
// remove legacy logger dependency
```

---

## Common Patterns

### Pattern 1: Severity Mapping

If your old framework uses severity levels, map them explicitly:

```cpp
void mapSeverity( std::uint64_t tagId, LegacyLogger& logger, const std::string& msg )
{
	using namespace kmac::nova::extras;
	switch ( tagId )
	{
		case kmac::nova::LoggerTraits< FatalTag >::tagId:
		case kmac::nova::LoggerTraits< ErrorTag >::tagId:
			logger.error( msg );
			break;
		case kmac::nova::LoggerTraits< WarningTag >::tagId:
			logger.warn( msg );
			break;
		case kmac::nova::LoggerTraits< DebugTag >::tagId:
		case kmac::nova::LoggerTraits< TraceTag >::tagId:
			logger.debug( msg );
			break;
		default:
			logger.info( msg );
			break;
	}
}
// call as: mapSeverity( record.tagId, logger, msg );
```

### Pattern 2: Subsystem Mapping

If your old framework uses named loggers:

```cpp
// extractSubsystem maps a Nova tag name to a legacy subsystem name.
// The tag name is the string passed as the second argument to NOVA_LOGGER_TRAITS.
// The right implementation depends on your naming convention - for example:
//   - tags named "AUDIO", "NETWORK" etc. - use the tag name directly as the subsystem
//   - tags named "AUDIO_DEBUG", "AUDIO.INFO" etc. - split on the first dividing character
//   - or use a switch on tagId for a compile-time mapping (most efficient)
std::string extractSubsystem( const char* tag )
{
	// example: split "AUDIO_DEBUG" -> "AUDIO"
	std::string name( tag );
	auto pos = name.find( '_' );
	return pos != std::string::npos ? name.substr( 0, pos ) : name;
}
```

```cpp
class SubsystemAdapterSink : public kmac::nova::Sink
{
private:
	std::map< std::string, LegacyLoggerPtr > _loggers;

public:
	void process( const kmac::nova::Record& record ) noexcept override
	{
		try {
			// extract subsystem from tag
			std::string subsystem = extractSubsystem( record.tag );

			// get or create logger for this subsystem
			auto it = _loggers.find( subsystem );
			if ( it == _loggers.end() ) {
				it = _loggers.emplace( subsystem, createLegacyLogger( subsystem ) ).first;
			}

			// forward to appropriate logger
			it->second->info( record.message );
		}
		catch ( ... ) {
			// silently ignore exception to maintain noexcept contract
		}
	}
};
```

### Pattern 3: Configuration Translation

Convert configuration files to C++ code:

```cpp
// old: log4cxx.properties
// log4j.rootLogger=INFO, console
// log4j.appender.console=org.apache.log4j.ConsoleAppender

// new: C++ configuration
OStreamSink console( std::cout );
kmac::nova::ScopedConfigurator config;
config.bind< InfoTag >( &console );
config.bind< WarningTag >( &console );
config.bind< ErrorTag >( &console );
```

Alternatively, custom config files can be managed by the application to configure logging, but at the expense of losing the zero-cost disabled loggers since all domains (tags) will likely need to be enabled, or ignore configuration for disabled domains.

---

## Troubleshooting

### Issue: Performance Regression

**Symptom:** logging slower after migration

**Solution:**
1. check if using StreamingRecordBuilder instead of TruncatingRecordBuilder
2. verify sinks aren't doing expensive operations
3. use NullSink to isolate builder overhead
4. disable unused tags at compile time

```cpp
// fast (stack-based)
NOVA_LOG( Tag ) << "Message";

// slower (heap allocation)
NOVA_LOG_STREAM( Tag ) << "Message";
```

### Issue: Missing Logs

**Symptom:** some logs don't appear

**Solution:**
1. check that tags are enabled: `LoggerTraits<Tag>::enabled = true`
2. verify sink is bound: `config.bind<Tag>(&sink)`
3. ensure sink's process() is implemented correctly
4. check tag names match

```cpp
// verify binding
auto* sink = Logger< Tag >::getSink();
if ( ! sink ) {
	std::cerr << "No sink bound for Tag!\n";
}
```

### Issue: Compilation Errors

**Symptom:** template errors, missing symbols

**Solution:**
1. include all necessary headers
2. specialize LoggerTraits for all tags - NOVA_LOGGER_TRAITS macro
4. check compiler support for your version of C++ - Nova supports C++11 and newer

```cpp
// correct trait definition
NOVA_LOGGER_TRAITS( ExampleTag, EXAMPLETAG, true, TimestampHelper::steadyNanosecs );
```

### Issue: Adapter Not Receiving Messages

**Symptom:** Nova logs aren't forwarded to legacy logger

**Solution:**
1. verify adapter sink is bound to tags
2. check noexcept compliance (adapter shouldn't throw)
3. add debug output in adapter's process() method

```cpp
void process( const Record& record ) noexcept override
{
	std::cerr << "Adapter received: " << record.tag << "\n";
	try {
		// ... forward to legacy logger
	}
	catch ( const std::exception& e ) {
		std::cerr << "Adapter error: " << e.what() << "\n";
	}
}
```

---

## Best Practices

1. **Start small** - migrate one module or subsystem first
2. **Use adapters** - don't do large-scale rewrites
3. **Test thoroughly** - verify log output matches before/after
4. **Document mappings** - keep a table of old logger names → Nova tags
5. **Parallel run** - keep both systems running during transition
6. **Monitor performance** - track logging overhead before/after
7. **Train team** - ensure everyone understands Nova's philosophy

---

## Summary

Migration to Nova can be done incrementally using the **Adapter Pattern**:

1. create a Nova sink that forwards to your existing logger
2. migrate modules one at a time
3. both systems can coexist during transition
4. remove adapter once migration complete

This approach minimizes risk and allows for gradual validation of Nova's behavior in your system.

For additional context see `docs/NOVA_README.md` and the working examples in `examples/`.
