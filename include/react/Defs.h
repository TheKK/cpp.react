#pragma once

////////////////////////////////////////////////////////////////////////////////////////
#define REACT_BEGIN		namespace react {
#define REACT_END		}
#define REACT			::react

#define REACT_IMPL_BEGIN	REACT_BEGIN		namespace impl {
#define REACT_IMPL_END		REACT_END		}
#define REACT_IMPL			REACT			::impl

#ifdef _DEBUG
#define REACT_MESSAGE(...) printf(__VA_ARGS__ ## "\n")
#else
#define REACT_MESSAGE
#endif

// Assert with message
#define REACT_ASSERT(condition, ...) for (; !(condition); assert(condition)) printf(__VA_ARGS__ ## "\n")

////////////////////////////////////////////////////////////////////////////////////////
// Type aliases
REACT_BEGIN

using uint = unsigned int;
using uchar = unsigned char;

REACT_END