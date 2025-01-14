#pragma once

#ifdef _MSC_VER
#pragma warning(disable : 4714)
#endif

#if defined(ERHE_PROFILE_LIBRARY_TRACY)
#   if defined(TRACY_ENABLE) && defined(ERHE_TRACY_GL)
#       include "erhe_gl/dynamic_load.hpp"
#       define glGenQueries          gl::glGenQueries
#       define glGetInteger64v       gl::glGetInteger64v
#       define glGetQueryiv          gl::glGetQueryiv
#       define glGetQueryObjectiv    gl::glGetQueryObjectiv
#       define glGetQueryObjectui64v gl::glGetQueryObjectui64v
#       define glQueryCounter        gl::glQueryCounter
#   endif
#   include <tracy/Tracy.hpp>

#   if defined(ERHE_TRACY_GL)
#       include "tracy/TracyOpenGL.hpp"
#   endif
#   if defined(TRACY_ENABLE) && defined(ERHE_TRACY_GL)
#       undef glGenQueries
#       undef glGetInteger64v
#       undef glGetQueryiv
#       undef glGetQueryObjectiv
#       undef glGetQueryObjectui64v
#       undef glQueryCounter
#   endif
#
#   define ERHE_PROFILE_FUNCTION() ZoneScoped
//#   define ERHE_PROFILE_FUNCTION() ZoneScopedS(62)
#   define ERHE_PROFILE_SCOPE(erhe_profile_id) ZoneScopedN(erhe_profile_id)
#   define ERHE_PROFILE_COLOR(erhe_profile_id, erhe_profile_color) ZoneScopedNC(erhe_profile_id, erhe_profile_color);
#   define ERHE_PROFILE_DATA(erhe_profile_id, erhe_profile_data, erhe_profile_data_length) ZoneName(erhe_profile_data, erhe_profile_data_length)
#   define ERHE_PROFILE_MESSAGE(erhe_profile_message, erhe_profile_message_length) TracyMessage(erhe_profile_message, erhe_profile_message_length)
#   define ERHE_PROFILE_MESSAGE_LITERAL(erhe_profile_message) TracyMessageL(erhe_profile_message)
#   define ERHE_PROFILE_GPU_SCOPE(erhe_profile_id) TracyGpuZone(erhe_profile_id.data())
#   define ERHE_PROFILE_GPU_CONTEXT TracyGpuContext
#   define ERHE_PROFILE_FRAME_END FrameMark; TracyGpuCollect
#   define ERHE_PROFILE_MUTEX_DECLARATION(Type, mutex_variable) tracy::Lockable<Type> mutex_variable
#   define ERHE_PROFILE_MUTEX(Type, mutex_variable) TracyLockable(Type, mutex_variable)
#   define ERHE_PROFILE_LOCKABLE_BASE(Type) LockableBase(Type)
#
#elif defined(ERHE_PROFILE_LIBRARY_SUPERLUMINAL) && defined(_WIN32)
#   include <PerformanceAPI.h>
#
#   define ERHE_PROFILE_FUNCTION PERFORMANCEAPI_INSTRUMENT_FUNCTION()
#   define ERHE_PROFILE_SCOPE(erhe_profile_id) PERFORMANCEAPI_INSTRUMENT(erhe_profile_id)
#   define ERHE_PROFILE_COLOR(erhe_profile_id, erhe_profile_color) PERFORMANCEAPI_INSTRUMENT_COLOR(erhe_profile_id, erhe_profile_color)
#   define ERHE_PROFILE_DATA(erhe_profile_id, erhe_profile_data, erhe_profile_data_length) PERFORMANCEAPI_INSTRUMENT_DATA(erhe_profile_id, erhe_profile_data)
#   define ERHE_PROFILE_MESSAGE(erhe_profile_message, erhe_profile_message_length)
#   define ERHE_PROFILE_MESSAGE_LITERAL(erhe_profile_message)
#   define ERHE_PROFILE_GPU_SCOPE(erhe_profile_id)
#   define ERHE_PROFILE_GPU_CONTEXT
#   define ERHE_PROFILE_FRAME_END
#   define ERHE_PROFILE_MUTEX_DECLARATION(Type, mutex_variable) Type mutex_variable
#   define ERHE_PROFILE_MUTEX(Type, mutex_variable) Type mutex_variable
#   define ERHE_PROFILE_LOCKABLE_BASE(Type) Type

#elif defined(ERHE_PROFILE_LIBRARY_NVTX)
#   define ERHE_CONCAT(x,y) ERHE_CONCAT_INDIRECT(x,y)
#   define ERHE_CONCAT_INDIRECT(x,y) x##y
#   define NVTX3_V1_FUNCTION_RANGE_IN(D)                                             \
        static ::nvtx3::v1::registered_string_in<D> const nvtx3_func_name__{__FUNCTION__}; \
        static ::nvtx3::v1::event_attributes const nvtx3_func_attr__{nvtx3_func_name__};   \
        ::nvtx3::v1::scoped_range_in<D> const nvtx3_range__{nvtx3_func_attr__};

#   include <nvtx3/nvtx3.hpp>
//#   define ERHE_PROFILE_FUNCTION() NVTX3_V1_FUNC_RANGE()
#   define ERHE_PROFILE_FUNCTION() NVTX3_V1_FUNCTION_RANGE_IN(::nvtx3::v1::domain::global)
#   define ERHE_PROFILE_SCOPE(name) nvtx3::scoped_range_in<nvtx3::domain::global> ERHE_CONCAT(__erhe_source_location,__LINE__){name}
#   define ERHE_PROFILE_COLOR(erhe_profile_id, erhe_profile_color) static_cast<void>(erhe_profile_id);
#   define ERHE_PROFILE_DATA(erhe_profile_id, erhe_profile_data, erhe_profile_data_length) static_cast<void>(erhe_profile_id);
#   define ERHE_PROFILE_MESSAGE(erhe_profile_message, erhe_profile_message_length) static_cast<void>(erhe_profile_message);
#   define ERHE_PROFILE_MESSAGE_LITERAL(erhe_profile_message) nvtx3::mark(erhe_profile_message)
#   define ERHE_PROFILE_GPU_SCOPE(erhe_profile_id) static_cast<void>(erhe_profile_id);
#   define ERHE_PROFILE_GPU_CONTEXT
#   define ERHE_PROFILE_FRAME_END
#   define ERHE_PROFILE_MUTEX_DECLARATION(Type, mutex_variable) Type mutex_variable
#   define ERHE_PROFILE_MUTEX(Type, mutex_variable) Type mutex_variable
#   define ERHE_PROFILE_LOCKABLE_BASE(Type) Type

#else
#   define ERHE_PROFILE_FUNCTION();
#   define ERHE_PROFILE_SCOPE(erhe_profile_id) static_cast<void>(erhe_profile_id);
#   define ERHE_PROFILE_COLOR(erhe_profile_id, erhe_profile_color) static_cast<void>(erhe_profile_id);
#   define ERHE_PROFILE_DATA(erhe_profile_id, erhe_profile_data, erhe_profile_data_length) static_cast<void>(erhe_profile_id);
#   define ERHE_PROFILE_MESSAGE(erhe_profile_message, erhe_profile_message_length) static_cast<void>(erhe_profile_message);
#   define ERHE_PROFILE_MESSAGE_LITERAL(erhe_profile_message) static_cast<void>(erhe_profile_message);
#   define ERHE_PROFILE_GPU_SCOPE(erhe_profile_id) static_cast<void>(erhe_profile_id);
#   define ERHE_PROFILE_GPU_CONTEXT
#   define ERHE_PROFILE_FRAME_END
#   define ERHE_PROFILE_MUTEX_DECLARATION(Type, mutex_variable) Type mutex_variable
#   define ERHE_PROFILE_MUTEX(Type, mutex_variable) Type mutex_variable
#   define ERHE_PROFILE_LOCKABLE_BASE(Type) Type
#endif
