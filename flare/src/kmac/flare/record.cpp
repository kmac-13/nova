#include "kmac/flare/record.h"

#include "kmac/flare/tlv.h"

namespace kmac::flare
{

void Record::clear() noexcept
{
	sequenceNumber = 0;
	status = 0;
	timestampNs = 0;
	tagId = 0;
	line = 0;
	processId = 0;
	threadId = 0;
	messageTruncated = false;
	file[ 0 ] = '\0';
	function[ 0 ] = '\0';
	message[ 0 ] = '\0';
	fileLen = 0;
	functionLen = 0;
	messageLen = 0;
}

const char* Record::statusString() const noexcept
{
	switch ( RecordStatus( status ) )
	{
		case RecordStatus::Complete: return "Complete";
		case RecordStatus::Truncated: return "Truncated";
		case RecordStatus::InProgress: return "InProgress (Torn Write)";
		case RecordStatus::Unknown: return "Unknown";
		default: return "Invalid";
	}
}

} // namespace kmac::flare
