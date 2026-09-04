#include "IMemoryFileStream.h"

#include <cstdint>

IMemoryFileStream::IMemoryFileStream(UInt64 inMaxSize)
:streamMaxSize(inMaxSize)
,streamIsDirty(false)
{

}

IMemoryFileStream::~IMemoryFileStream()
{
	// a flush failure here cannot be reported to a caller; the pending
	// in-memory data is lost with the object, so log it
	if(!Close())
	{
		_ERROR("IMemoryFileStream: failed to flush %s on destruction, pending data was lost", streamPath.c_str());
	}
}

void IMemoryFileStream::SetPath(const char * name)
{
	if(name)
	{
		streamPath = name;
	}
}

bool IMemoryFileStream::Open(const char * name)
{
	if(!Close())
	{
		return false;
	}

	IFileStream	file;

	if(!file.Open(name))
	{
		return false;
	}

	UInt64 fileLength = (UInt64)file.GetLength();

	if(streamMaxSize && (fileLength > streamMaxSize))
	{
		return false;
	}

	// prepare the image at the file's size (zeroed, offset 0, not dirty);
	// ReadBuf takes a UInt32 length, so files past 4 GiB are read in chunks
	Discard(fileLength);

	ReadChunked(file, streamBuffer.data(), fileLength);

	// IFileStream::ReadBuf does not report I/O errors: a failed read leaves
	// the file's offset short of the end and the buffer's tail zeroed. The
	// path is only committed after the read is verified complete.
	if(file.GetOffset() != (SInt64)fileLength)
	{
		Discard();

		return false;
	}

	SetPath(name);

	return true;
}

bool IMemoryFileStream::Create(const char * name)
{
	// a failed pending flush keeps the image (see Close); refusing to discard
	// it, so Create reports the failure instead of starting over
	if(!Close())
	{
		return false;
	}

	// no target path -> nothing to create. Matches IFileStream::Create (which
	// fails on a null name) instead of claiming success on an empty path that a
	// later Close() would then trip on inside Flush()
	if(!name)
	{
		return false;
	}

	SetPath(name);

	return true;
}

bool IMemoryFileStream::Close(void)
{
	if(streamIsDirty && !Flush())
	{
		// keep the in-memory image (and the dirty flag) so the data can be
		// inspected and Close() retried; the failed Flush left no file
		// behind
		return false;
	}

	Discard();

	return true;
}

void IMemoryFileStream::Discard(UInt64 inNewSize)
{
	streamBuffer.clear();

	streamLength = 0;
	streamOffset = 0;
	streamIsDirty = false;

	// optionally re-create the image zeroed at the requested size; going
	// through SetLength inherits its maxSize / SInt64-range guards
	if(inNewSize)
	{
		SetLength(inNewSize);
	}
}

void IMemoryFileStream::ReadChunked(IFileStream & file, UInt8 * dst, UInt64 length)
{
	const UInt64	maxChunk = UINT32_MAX;

	UInt64	remaining = length;
	while(remaining)
	{
		UInt32	chunk = (UInt32)(remaining < maxChunk ? remaining : maxChunk);
		file.ReadBuf(dst, chunk);
		dst += chunk;
		remaining -= chunk;
	}
}

void IMemoryFileStream::WriteChunked(IFileStream & file, const UInt8 * src, UInt64 length)
{
	const UInt64	maxChunk = UINT32_MAX;

	UInt64	remaining = length;
	while(remaining)
	{
		UInt32	chunk = (UInt32)(remaining < maxChunk ? remaining : maxChunk);
		file.WriteBuf(src, chunk);
		src += chunk;
		remaining -= chunk;
	}
}

bool IMemoryFileStream::Flush(void)
{
	if(streamPath.empty())
	{
		return false;
	}

	IFileStream	file;

	if(!file.Create(streamPath.c_str()))
	{
		return false;
	}

	// WriteBuf takes a UInt32 length, so images past 4 GiB are committed in
	// chunks
	WriteChunked(file, streamBuffer.data(), (UInt64)streamLength);

	file.Close();

	// IFileStream::WriteBuf does not report short writes, so confirm the bytes
	// actually reached disk; a disk-full or I/O error would otherwise be
	// reported as a success here, committing a truncated file.
	IFileStream	check;

	if(!check.Open(streamPath.c_str()))
	{
		// whatever is on disk is unusable; remove it to honour the
		// all-or-nothing promise
		DeleteFile(streamPath.c_str());

		return false;
	}

	if((UInt64)check.GetLength() != (UInt64)streamLength)
	{
		DeleteFile(streamPath.c_str());

		return false;
	}

	return true;
}

void IMemoryFileStream::ReadBuf(void * buf, UInt32 inLength)
{
	ASSERT_STR(buf != NULL, "IMemoryFileStream::ReadBuf: destination is null");
	ASSERT_STR((UInt64)streamOffset + inLength <= (UInt64)streamLength, "IMemoryFileStream::ReadBuf: read past end of file");

	memcpy(buf, streamBuffer.data() + streamOffset, inLength);
	streamOffset += inLength;
}

void IMemoryFileStream::WriteBuf(const void * buf, UInt32 inLength)
{
	if(inLength == 0)
	{
		return;
	}

	ASSERT_STR(buf != NULL, "IMemoryFileStream::WriteBuf: source is null");

	UInt64 required = (UInt64)streamOffset + inLength;

	// the image is addressed by a SInt64 length, so a write that would end past
	// 2^63-1 (e.g. after an extreme SetOffset) is unrepresentable; refuse it the
	// way SetLength() does instead of letting streamBuffer.resize() throw
	ASSERT_STR(required <= (UInt64)INT64_MAX, "IMemoryFileStream::WriteBuf: write exceeds representable size");

	ASSERT_STR(streamMaxSize == 0 || required <= streamMaxSize, "IMemoryFileStream::WriteBuf: write exceeds max size");

	if(required > streamBuffer.size())
	{
		streamBuffer.resize(required);
	}

	memcpy(streamBuffer.data() + streamOffset, buf, inLength);
	streamOffset += inLength;

	if(streamLength < (SInt64)streamOffset)
	{
		streamLength = streamOffset;
	}

	streamIsDirty = true;
}

void IMemoryFileStream::SetOffset(SInt64 inOffset)
{
	if(inOffset < 0)
	{
		inOffset = 0;
	}

	// Not clamped to streamLength on purpose: callers reserve regions ahead of
	// the current end (e.g. Skip(sizeof header) then fill it later), and
	// WriteBuf grows the image as needed. Reading past the end is still
	// guarded by ReadBuf.
	streamOffset = inOffset;
}

void IMemoryFileStream::SetLength(UInt64 inLength)
{
	// streamLength is a SInt64, so it cannot name a length past 2^63-1
	ASSERT_STR(inLength <= (UInt64)INT64_MAX, "IMemoryFileStream::SetLength: length too large");

	// honour the same limit that Open and WriteBuf enforce
	ASSERT_STR(streamMaxSize == 0 || inLength <= streamMaxSize, "IMemoryFileStream::SetLength: length exceeds max size");

	streamBuffer.resize(inLength);

	streamLength = (SInt64)inLength;

	if(streamOffset > streamLength)
	{
		streamOffset = streamLength;
	}
}
