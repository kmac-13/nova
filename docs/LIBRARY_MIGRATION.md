# Migrating to Nova from Other Logging Frameworks

This guide provides step-by-step instructions for migrating from popular logging frameworks to Nova, including strategies for gradual migration and coexistence.

## Table of Contents

1. [Migration Strategies](#migration-strategies)
2. [From spdlog](#from-spdlog)
3. [From glog](#from-glog)
4. [From Boost.Log](#from-boostlog)
5. [From log4cxx/log4cpp](#from-log4cxxlog4cpp)
6. [From easylogging++](#from-easylogging)
7. [Adapter Pattern for Gradual Migration](#adapter-pattern-for-gradual-migration)
8. [Common Patterns](#common-patterns)
9. [Troubleshooting](#troubleshooting)

---

## Migration Strategies

### Strategy 1: Complete Replacement (Greenfield)

Best for: new projects or small codebases

1. remove old logging library dependencies
2. add Nova headers
3. define tags for your subsystems/severities
4. configure sinks
5. replace log calls with Nova macros

---

### Strategy 2: Gradual Migration (Adapter Pattern)

Best for: large codebases, production systems, risk-averse environments

Use Nova sinks that **adapt** to your existing logging framework, allowing both systems to coexist:

```cpp
// Nova sink that forwards to spdlog
class SpdlogAdapterSink : public nova::Sink {
	std::shared_ptr< spdlog::logger > _logger;
public:
	void process( const Record& record ) noexcept override {
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

auto logger = spdlog::stdout_color_mt( "mylogger" );
logger->set_level( spdlog::level::debug );

logger->info( "Server started on port {}", 8080 );
logger->debug( "Processing request from {}", clientIP );
logger->error( "Failed to connect: {}", errorMsg );
```

**After (Nova):**
```cpp
#include <kmac/nova/macros.h>
#include <kmac/nova/scoped_configurator.h>
#include <kmac/nova/extras/ostream_sink.h>
#include <kmac/nova/extras/severities.h>

using namespace kmac::nova::extras;

// configure sinks
OStreamSink console( std::cout );
ScopedConfigurator config;
config.bind< InfoTag >( &console );
config.bind< DebugTag >( &console );
config.bind< ErrorTag >( &console );

// log messages
NOVA_LOG_I() << "Server started on port " << 8080;
NOVA_LOG_D() << "Processing request from " << clientIP;
NOVA_LOG_E() << "Failed to connect: " << errorMsg;
```

### Adapter Sink for Gradual Migration

```cpp
#include <spdlog/spdlog.h>
#include <kmac/nova/sink.h>

class SpdlogAdapterSink : public kmac::nova::Sink
{
	std::shared_ptr<spdlog::logger> _logger;
    
public:
	explicit SpdlogAdapterSink( std::shared_ptr< spdlog::logger > logger )
		: _logger( std::move( logger ) )
	{}

	void process( const kmac::nova::Record& record ) noexcept override
	{
		try {
			// map Nova tags to spdlog levels (simple example)
			if ( std::strstr( record.tag, "ERROR" ) || std::strstr( record.tag, "FATAL" ) ) {
				_logger->error( "[{}] {}:{} {}",
					record.tag, record.file, record.line, record.message );
			}
			else if ( std::strstr( record.tag, "WARN" ) ) {
				_logger->warn( "[{}] {}:{} {}",
					record.tag, record.file, record.line, record.message );
			}
			else if ( std::strstr(record.tag, "DEBUG" ) ) {
				_logger->debug( "[{}] {}:{} {}",
					record.tag, record.file, record.line, record.message );
			}
			else {
				_logger->info( "[{}] {}:{} {}",
					record.tag, record.file, record.line, record.message );
			}
		}
		catch ( ... ) {
			// spdlog might throw - catch to maintain noexcept
		}
	}
};

// usage:
auto spdlog_logger = spdlog::stdout_color_mt( "myapp" );
SpdlogAdapterSink adapter( spdlog_logger );

ScopedConfigurator config;
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
#include <kmac/nova/macros.h>
#include <kmac/nova/scoped_configurator.h>
#include <kmac/nova/extras/ostream_sink.h>
#include <kmac/nova/extras/severities.h>

using namespace kmac::nova::extras;

// configure sinks
OStreamSink console( std::cerr );  // glog defaults to stderr
ScopedConfigurator config;
config.bind< InfoTag >( &console );
config.bind< WarningTag >( &console );
config.bind< ErrorTag >( &console );

// log messages
NOVA_LOG_I() << "Server starting";
NOVA_LOG_W() << "Low memory: " << freeMemory << " bytes";
NOVA_LOG_E() << "Connection failed: " << error;

// for VLOG - define custom verbosity tags
struct VerboseTag {};
NOVA_LOGGER_TRAITS( VerboseTag, VERBOSE, true, /* timestamp */ );
NOVA_LOG( VerboseTag ) << "Detailed trace info";
```

### Adapter Sink for Gradual Migration

```cpp
#include <glog/logging.h>
#include <kmac/nova/sink.h>

class GlogAdapterSink : public kmac::nova::Sink
{
public:
	void process( const kmac::nova::Record& record ) noexcept override
	{
		try {
			// map to glog severity
			if ( std::strstr( record.tag, "ERROR" ) || std::strstr( record.tag, "FATAL" ) ) {
				LOG( ERROR ) << "[" << record.tag << "] " << record.message;
			}
			else if ( std::strstr( record.tag, "WARN" ) ) {
				LOG( WARNING ) << "[" << record.tag << "] " << record.message;
			}
			else {
				LOG( INFO ) << "[" << record.tag << "] " << record.message;
			}
		}
		catch ( ... ) {
			// glog might throw - catch to maintain noexcept
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
| Attribute | logger_traits |
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
#include <kmac/nova/extras/severities.h>
#include <kmac/nova/extras/ostream_sink.h>

using namespace kmac::nova::extras;

// configure sinks
OStreamSink console( std::cout );
kmac::nova::ScopedConfigurator config;
config.bind< TraceTag >( &console );
config.bind< InfoTag >( &console );
config.bind< ErrorTag >( &console );

// log messages
NOVA_LOG_T() << "Entering function";
NOVA_LOG_I() << "Server started";
NOVA_LOG_E() << "Connection lost";
```

### Adapter Sink

```cpp
#include <boost/log/trivial.hpp>
#include <kmac/nova/sink.h>

class BoostLogAdapterSink : public kmac::nova::Sink
{
public:
	void process(const kmac::nova::Record& record) noexcept override
	{
		try {
			// forward to Boost.Log
			if ( std::strstr( record.tag, "TRACE" ) ) {
				BOOST_LOG_TRIVIAL( trace ) << "[" << record.tag << "] " << record.message;
			}
			else if ( std::strstr( record.tag, "DEBUG" ) ) {
				BOOST_LOG_TRIVIAL( debug ) << "[" << record.tag << "] " << record.message;
			}
			else if ( std::strstr( record.tag, "INFO" ) ) {
				BOOST_LOG_TRIVIAL( info ) << "[" << record.tag << "] " << record.message;
			}
			else if ( std::strstr( record.tag, "WARN" ) ) {
				BOOST_LOG_TRIVIAL( warning ) << "[" << record.tag << "] " << record.message;
			}
			else if ( std::strstr( record.tag, "ERROR" ) ) {
				BOOST_LOG_TRIVIAL( error ) << "[" << record.tag << "] " << record.message;
			}
			else if ( std::strstr( record.tag, "FATAL" ) ) {
				BOOST_LOG_TRIVIAL( fatal ) << "[" << record.tag << "] " << record.message;
			}
			else {
				BOOST_LOG_TRIVIAL( info ) << "[" << record.tag << "] " << record.message;
			}
		}
		catch ( ... ) { }
	}
};
```

---

## From log4cxx/log4cpp

### Conceptual Mapping

| log4cxx Concept | Nova Equivalent |
|-----------------|-----------------|
| `Logger::getLogger("name")` | `Logger<Tag>` |
| `logger->info()` | `NOVA_LOG(InfoTag)` |
| Appender | Sink |
| Layout | FormattingSink |
| Configuration file | Explicit C++ code |

### Migration Steps

**Before (log4cxx):**
```cpp
#include <log4cxx/basicconfigurator.h>
#include <log4cxx/logger.h>

using namespace log4cxx;

LoggerPtr logger( Logger::getLogger( "MyApp.Network" ) );
BasicConfigurator::configure();

LOG4CXX_INFO( logger, "Connection established" );
LOG4CXX_DEBUG( logger, "Bytes received: " << count );
LOG4CXX_ERROR( logger, "Timeout occurred" );
```

**After (Nova):**
```cpp
#include <kmac/nova/macros.h>
#include <kmac/nova/scoped_configurator.h>
#include <kmac/nova/extras/hierarchical_tag.h>
#include <kmac/nova/extras/severities.h>

using namespace kmac::nova::extras;

// define subsystem
struct NetworkSubsystem {};

// use hierarchical tags for subsystem + severity
using NetworkInfo = HierarchicalTag< NetworkSubsystem, InfoTag >;
using NetworkDebug = HierarchicalTag< NetworkSubsystem, DebugTag >;
using NetworkError = HierarchicalTag< NetworkSubsystem, ErrorTag >;

// configure sinks
OStreamSink console( std::cout );
ScopedConfigurator config;
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
#include <log4cxx/logger.h>
#include <kmac/nova/sink.h>

class Log4cxxAdapterSink : public kmac::nova::Sink
{
	log4cxx::LoggerPtr _logger;

public:
	explicit Log4cxxAdapterSink( const std::string& loggerName )
		: _logger( log4cxx::Logger::getLogger( loggerName ) )
	{}

	void process( const kmac::nova::Record& record ) noexcept override
	{
		try {
			std::string msg = std::string( record.message, record.messageLength);

			if ( std::strstr( record.tag, "ERROR" ) ) {
				LOG4CXX_ERROR( _logger, msg );
			}
			else if ( std::strstr(record.tag, "WARN" ) ) {
				LOG4CXX_WARN( _logger, msg );
			}
			else if ( std::strstr( record.tag, "DEBUG" ) ) {
				LOG4CXX_DEBUG( _logger, msg );
			}
			else {
				LOG4CXX_INFO( _logger, msg );
			}
		}
		catch ( ... ) { }
	}
};
```

---

## From easylogging++

### Conceptual Mapping

| easylogging++ Concept | Nova Equivalent |
|-----------------------|-----------------|
| `LOG(INFO)` | `NOVA_LOG(InfoTag)` |
| `VLOG(level)` | Custom verbosity tags |
| Configuration file | Explicit C++ code |
| Logger ID | Tag type |

### Migration Steps

**Before (easylogging++):**
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
#include <kmac/nova/macros.h>
#include <kmac/nova/scoped_configurator.h>
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
NOVA_LOG_I() << "Application started";
NOVA_LOG_D() << "Processing item: " << itemId;
NOVA_LOG_E() << "Database error: " << err;

// define custom verbosity tag
struct VerboseTag {};
NOVA_LOGGER_TRAITS( VerboseTag, VERBOSE, true, kmac::nova::TimestampHelper::steadyNanosecs );
config.bind< VerboseTag >( &console );
NOVA_LOG( VerboseTag ) << "Verbose trace";
```

### Adapter Sink

```cpp
#include <easylogging++.h>
#include <kmac/nova/sink.h>

class EasyloggingAdapterSink : public kmac::nova::Sink
{
public:
	void process( const kmac::nova::Record& record ) noexcept override
	{
		try {
			// map to easylogging++ levels
			if ( std::strstr( record.tag, "TRACE" ) ) {
				VLOG( 9 ) << "[" << record.tag << "] " << record.message;
			}
			else if ( std::strstr( record.tag, "DEBUG" ) ) {
				LOG( DEBUG ) << "[" << record.tag << "] " << record.message;
			}
			else if ( std::strstr( record.tag, "INFO" ) ) {
				LOG( INFO ) << "[" << record.tag << "] " << record.message;
			}
			else if ( std::strstr( record.tag, "WARN" ) ) {
				LOG( WARNING ) << "[" << record.tag << "] " << record.message;
			}
			else if ( std::strstr( record.tag, "ERROR" ) ) {
				LOG( ERROR ) << "[" << record.tag << "] " << record.message;
			}
			else if ( std::strstr( record.tag, "FATAL" ) ) {
				LOG( FATAL ) << "[" << record.tag << "] " << record.message;
			}
			else {
				LOG( INFO ) << "[" << record.tag << "] " << record.message;
			}
		}
		catch ( ... ) { }
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
#include <kmac/nova/sink.h>

template< typename LegacyLogger >
class LegacyLoggerAdapter : public kmac::nova::Sink
{
	LegacyLogger& _legacyLogger;

public:
	explicit LegacyLoggerAdapter( LegacyLogger& logger )
		: _legacyLogger( logger )
	{}

	void process( const kmac::nova::Record& record ) noexcept override
	{
		try {
			// extract severity from tag name (if using severity-based tags)
			bool isError = std::strstr( record.tag, "ERROR" )
				|| std::strstr( record.tag, "FATAL" );
			bool isWarn = std::strstr( record.tag, "WARN" );
			bool isDebug = std::strstr( record.tag, "DEBUG" );

			// format message
			std::string formatted = formatMessage( record );

			// forward to legacy logger
			if ( isError ) {
				_legacyLogger.error( formatted );
			}
			else if ( isWarn ) {
				_legacyLogger.warn( formatted );
			}
			else if ( isDebug ) {
				_legacyLogger.debug( formatted );
			}
			else {
				_legacyLogger.info( formatted );
			}
		}
		catch ( ... ) {
			// maintain noexcept contract
		}
	}

private:
	std::string formatMessage( const kmac::nova::Record& record )
	{
		std::ostringstream oss;
		oss << "[" << record.tag << "] "
			<< record.file << ":" << record.line << " "
			<< std::string( record.message, record.messageLength );
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
ScopedConfigurator config;
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

// both outputs go through legacy logger
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
void mapSeverity( const char* tag, LegacyLogger& logger, const std::string& msg )
{
	if ( std::strstr( tag, "FATAL" ) ) {
		logger.fatal( msg );
	}
	else if ( std::strstr( tag, "ERROR" ) ) {
		logger.error( msg );
	}
	else if ( std::strstr( tag, "WARN" ) ) {
		logger.warn( msg );
	}
	else if ( std::strstr( tag, "INFO" ) ) {
		logger.info( msg );
	}
	else if ( std::strstr( tag, "DEBUG" ) ) {
		logger.debug( msg );
	}
	else if ( std::strstr( tag, "TRACE" ) ) {
		logger.trace( msg );
	}
	else {
		logger.info( msg );  // default
	}
}
```

### Pattern 2: Subsystem Mapping

If your old framework uses named loggers:

```cpp
class SubsystemAdapterSink : public nova::Sink
{
	std::map< std::string, LegacyLoggerPtr > _loggers;

public:
	void process(const nova::Record& record) noexcept override
	{
		try {
			// extract subsystem from tag
			std::string subsystem = extractSubsystem( record.tag );

			// get or create logger for this subsystem
			auto it = _loggers.find( subsystem );
			if ( it == _loggers.end() ) {
				it = _loggers.emplace( subsystem, 
					createLegacyLogger( subsystem ) ).first;
			}

			// forward to appropriate logger
			it->second->info( record.message );
		}
		catch ( ... ) { }
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
ScopedConfigurator config;
config.bind< InfoTag >( &console );
config.bind< WarningTag >( &console );
config.bind< ErrorTag >( &console );
```

Alternatively, custom config files can be managed by the application to configure logging, but at the expense of losing the zero-cost disabled loggers since all tags will likely need to be enabled.

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
1. check that tags are enabled: `logger_traits<Tag>::enabled = true`
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
2. specialize logger_traits for all tags
3. use NOVA_LOGGER_TRAITS macro
4. check C++17 compiler support

```cpp
// correct trait definition
NOVA_LOGGER_TRAITS( MyTag, MYTAG, true, TimestampHelper::steadyNanosecs );
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

For questions or assistance, see the examples in `examples/` or consult the documentation in `docs/`.
