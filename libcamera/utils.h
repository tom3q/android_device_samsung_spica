#ifndef _LIBCAMERA_UTILS_H_
#define _LIBCAMERA_UTILS_H_

#define DEBUG
#define DEBUG_TRACE

#ifdef DEBUG
#define DBG(format, ...) \
			LOGD("DBG(%s): " format, __func__, ##__VA_ARGS__)
#else
#define DBG(format, ...)
#endif

#ifdef DEBUG_TRACE
class Tracer {
	const char *name;
	static int level;
public:
	Tracer(const char *name) :
		name(name)
	{
		++level;
		LOG(LOG_VERBOSE, "Tracer", "%*s %s enter", 2*level, ">", name);
	}

	~Tracer()
	{
		LOG(LOG_VERBOSE, "Tracer", "%*s %s leave", 2*level, "<", name);
		--level;
	}
};
#define TRACE() \
			Tracer __tracer__LINE__(__func__)
#else
#define TRACE()
#endif

#define ERR(format, ...) \
			LOGE("ERR(%s): " format, __func__, ##__VA_ARGS__)

template <typename T>
static inline T max(const T &a, const T &b)
{
	if (b > a)
		return b;
	return a;
}

#define ARRAY_SIZE(x) (sizeof((x)) / sizeof((x)[0]))

#endif /* _LIBCAMERA_UTILS_H_ */
