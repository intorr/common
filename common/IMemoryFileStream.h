#pragma once

#include "common/IDataStream.h"
#include "common/IFileStream.h"

#include <string>
#include <vector>

/**
 *	A whole-file in-memory stream.
 *
 *	Backs an entire file with RAM so that arbitrarily many small reads,
 *	writes, and seeks are served from memory, and the disk is touched
 *	exactly once: the file is read fully on Open(), and (if any data was
 *	written) created and written fully on Close(). This makes a save
 *	all-or-nothing -- a file only appears on disk once the whole in-memory
 *	image is complete, and a failed write removes the partial file instead
 *	of leaving it behind. A failed Close() keeps the image in memory (still
 *	dirty) so it can be retried; Discard() drops it without touching the
 *	disk at all. A flush that still fails when the object is destroyed is
 *	logged, since the pending data is lost with the object.
 *
 *	Not a general-purpose streaming buffer: it always holds the whole file,
 *	so it is intended for small files, and it is not thread-safe.
 */
class IMemoryFileStream : public IDataStream
{
	public:
		static const UInt64	kNoSizeLimit = 0;

		// pass a non-zero maxSize to refuse to buffer any file larger than it
		IMemoryFileStream(UInt64 inMaxSize = kNoSizeLimit);
		virtual ~IMemoryFileStream();

		// the stream owns a whole-file image and a single target path; copying
		// it would create two objects that both flush to the same path, so it is
		// deliberately non-copyable
		IMemoryFileStream(const IMemoryFileStream &) = delete;
		IMemoryFileStream & operator=(const IMemoryFileStream &) = delete;

		// both flush any pending image first; if that flush fails they return
		// false and the image is kept (still dirty, path unchanged) for a retry
		bool	Open(const char * name);
		bool	Create(const char * name);

		// flushes any pending writes to disk; reports whether that succeeded.
		// On failure the partial file is removed and the in-memory image is
		// kept (still dirty) so Close() can be retried
		bool	Close(void);

		// drops any pending in-memory data without touching the disk; a later
		// Close() is a no-op. Optionally re-creates the image zeroed at
		// inNewSize (default 0: the image is dropped entirely)
		void	Discard(UInt64 inNewSize = 0);

		virtual void	ReadBuf(void * buf, UInt32 inLength);
		virtual void	WriteBuf(const void * buf, UInt32 inLength);
		virtual void	SetOffset(SInt64 inOffset);

		// resizes the in-memory image. It does NOT mark the stream dirty, so a
		// SetLength-only Close() writes nothing to disk (the resize lives only in
		// memory); a later WriteBuf() is what marks the image dirty and commits it.
		// To shrink/truncate a file on disk use Discard() or Open(), not
		// SetLength() + Close().
		void	SetLength(UInt64 inLength);

	private:
		void	SetPath(const char * name);
		bool	Flush(void);

		// IDataStream::ReadBuf/WriteBuf take a UInt32 length, so transfers of
		// more than 4 GiB go through in chunks
		void	ReadChunked(IFileStream & file, UInt8 * dst, UInt64 length);
		void	WriteChunked(IFileStream & file, const UInt8 * src, UInt64 length);

		std::vector<UInt8>	streamBuffer;
		std::string		streamPath;

		UInt64		streamMaxSize;
		bool		streamIsDirty;
};
