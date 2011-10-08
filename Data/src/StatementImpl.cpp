//
// StatementImpl.cpp
//
// $Id: //poco/1.4/Data/src/StatementImpl.cpp#1 $
//
// Library: Data
// Package: DataCore
// Module:  StatementImpl
//
// Copyright (c) 2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
// 
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//


#include "Poco/Data/StatementImpl.h"
#include "Poco/Data/DataException.h"
#include "Poco/Data/AbstractBinder.h"
#include "Poco/Data/Extraction.h"
#include "Poco/Data/BLOB.h"
#include "Poco/SharedPtr.h"
#include "Poco/String.h"
#include "Poco/Exception.h"


namespace Poco {
namespace Data {


StatementImpl::StatementImpl():
	_state(ST_INITIALIZED),
	_extrLimit(upperLimit((Poco::UInt32) Limit::LIMIT_UNLIMITED, false)),
	_lowerLimit(0),
	_columnsExtracted(0),
	_ostr(),
	_bindings()
{
}


StatementImpl::~StatementImpl()
{
}


Poco::UInt32 StatementImpl::execute()
{
	resetExtraction();
	Poco::UInt32 lim = 0;
	if (_lowerLimit > _extrLimit.value())
		throw LimitException("Illegal Statement state. Upper limit must not be smaller than the lower limit.");

	if (_extrLimit.value() == Limit::LIMIT_UNLIMITED)
		lim = executeWithoutLimit();
	else
		lim = executeWithLimit();
		
	if (lim < _lowerLimit)
	{
		throw LimitException("Did not receive enough data.");
	}
	return lim;
}


Poco::UInt32 StatementImpl::executeWithLimit()
{
	poco_assert (_state != ST_DONE);

	compile();

	Poco::UInt32 count = 0;
	Poco::UInt32 limit = _extrLimit.value();
	do
	{
		bind();
		while (hasNext() && count < limit)
		{
			next();
			++count;
		}
	}
	while (canBind() && count < limit);

	if (!canBind() && (!hasNext() || 0 == limit))
		_state = ST_DONE;
	else if (hasNext() && limit == count && _extrLimit.isHardLimit())
		throw LimitException("HardLimit reached. We got more data than we asked for");

	return count;
}


Poco::UInt32 StatementImpl::executeWithoutLimit()
{
	poco_assert (_state != ST_DONE);
	
	compile();

	Poco::UInt32 count = 0;
	do
	{
		bind();
		while (hasNext())
		{
			next();
			++count;
		}
	}
	while (canBind());

	_state = ST_DONE;
	return count;
}


void StatementImpl::compile()
{
	if (_state == ST_INITIALIZED)
	{
		compileImpl();
		_state = ST_COMPILED;

		if (!extractions().size())
		{
			Poco::UInt32 cols = columnsReturned();
			if (cols) makeExtractors(cols);
		}

		fixupExtraction();
		fixupBinding();
	}
	else if (_state == ST_RESET)
	{
		resetBinding();
		resetExtraction();
		_state = ST_COMPILED;
	}
}


void StatementImpl::bind()
{
	if (_state == ST_COMPILED)
	{
		bindImpl();
		_state = ST_BOUND;
	}
	else if (_state == ST_BOUND)
	{
		if (!hasNext())
		{
			if (canBind())
			{
				bindImpl();
			}
			else
				_state = ST_DONE;
		}
	}
}


void StatementImpl::reset()
{
	_state = ST_RESET;
	compile();
}


void StatementImpl::setExtractionLimit(const Limit& extrLimit)
{
	if (!extrLimit.isLowerLimit())
		_extrLimit = extrLimit;
	else
		_lowerLimit = extrLimit.value();
}


void StatementImpl::fixupExtraction()
{
	Poco::Data::AbstractExtractionVec::iterator it    = extractions().begin();
	Poco::Data::AbstractExtractionVec::iterator itEnd = extractions().end();
	AbstractExtractor& ex = extractor();
	_columnsExtracted = 0;
	for (; it != itEnd; ++it)
	{
		(*it)->setExtractor(&ex);
		(*it)->setLimit(_extrLimit.value()),
		_columnsExtracted += (int)(*it)->numOfColumnsHandled();
	}
}


void StatementImpl::fixupBinding()
{
	// no need to call binder().reset(); here will be called before each bind anyway
	AbstractBindingVec::iterator it    = bindings().begin();
	AbstractBindingVec::iterator itEnd = bindings().end();
	AbstractBinder& bin = binder();
	std::size_t numRows = 0;
	if (it != itEnd)
		numRows = (*it)->numOfRowsHandled();
	for (; it != itEnd; ++it)
	{
		if (numRows != (*it)->numOfRowsHandled())
		{
			throw BindingException("Size mismatch in Bindings. All Bindings MUST have the same size");
		}
		(*it)->setBinder(&bin);
	}
}


void StatementImpl::resetBinding()
{
	AbstractBindingVec::iterator it    = bindings().begin();
	AbstractBindingVec::iterator itEnd = bindings().end();
	for (; it != itEnd; ++it)
	{
		(*it)->reset();
	}
}


void StatementImpl::resetExtraction()
{
	Poco::Data::AbstractExtractionVec::iterator it = extractions().begin();
	Poco::Data::AbstractExtractionVec::iterator itEnd = extractions().end();
	for (; it != itEnd; ++it)
	{
		(*it)->reset();
	}
}


void StatementImpl::makeExtractors(Poco::UInt32 count)
{
	for (int i = 0; i < count; ++i)
	{
		const MetaColumn& mc = metaColumn(i);
		switch (mc.type())
		{
			case MetaColumn::FDT_BOOL:
			case MetaColumn::FDT_INT8:   
				addInternalExtract<Int8, std::vector<Int8> >(mc); 
				break;
			case MetaColumn::FDT_UINT8:  
				addInternalExtract<UInt8, std::vector<UInt8> >(mc); 
				break;
			case MetaColumn::FDT_INT16:  
				addInternalExtract<Int16, std::vector<Int16> >(mc); 
				break;
			case MetaColumn::FDT_UINT16: 
				addInternalExtract<UInt16, std::vector<UInt16> >(mc); 
				break;
			case MetaColumn::FDT_INT32:  
				addInternalExtract<Int32, std::vector<Int32> >(mc); 
				break;
			case MetaColumn::FDT_UINT32: 
				addInternalExtract<UInt32, std::vector<UInt32> >(mc); 
				break;
			case MetaColumn::FDT_INT64:  
				addInternalExtract<Int64, std::vector<Int64> >(mc); 
				break;
			case MetaColumn::FDT_UINT64: 
				addInternalExtract<UInt64, std::vector<UInt64> >(mc); 
				break;
			case MetaColumn::FDT_FLOAT:  
				addInternalExtract<float, std::vector<float> >(mc); 
				break;
			case MetaColumn::FDT_DOUBLE: 
				addInternalExtract<double, std::vector<double> >(mc); 
				break;
			case MetaColumn::FDT_STRING: 
				addInternalExtract<std::string, std::vector<std::string> >(mc); 
				break;
			case MetaColumn::FDT_BLOB:   
				addInternalExtract<BLOB, std::vector<BLOB> >(mc); 
				break;
			default:
				throw Poco::InvalidArgumentException("Data type not supported.");
		}
	}
}


const MetaColumn& StatementImpl::metaColumn(const std::string& name) const
{
	Poco::UInt32 cols = columnsReturned();
	for (Poco::UInt32 i = 0; i < cols; ++i)
	{
		const MetaColumn& column = metaColumn(i);
		if (0 == icompare(column.name(), name)) return column;
	}

	throw NotFoundException(format("Invalid column name: %s", name));
}


} } // namespace Poco::Data
