#pragma once

#include "common/IDataStream.h"

/**
 *	An input/output file stream
 */
class IFileStream : public IDataStream
{
	public:
		IFileStream();
		IFileStream(const char * name);
		~IFileStream();

		virtual bool	Open(const char * name);
		bool			BrowseOpen(void);

		virtual bool	Create(const char * name);
		bool			BrowseCreate(const char * defaultName = NULL, const char * defaultPath = NULL, const char * title = NULL);

		virtual void	Close(void);

		HANDLE			GetHandle(void)	{ return theFile; }

		virtual void	ReadBuf(void * buf, UInt32 inLength);
		virtual void	WriteBuf(const void * buf, UInt32 inLength);
		virtual void	SetOffset(SInt64 inOffset);

		// can truncate. implicitly seeks to the end of the file
		virtual void	SetLength(UInt64 length);

		static void	MakeAllDirs(const char * path);
		static char * ExtractFileName(char * path);

	protected:
		HANDLE	theFile;
};
