/*
   +----------------------------------------------------------------------+
   | HipHop for PHP                                                       |
   +----------------------------------------------------------------------+
   | Copyright (c) 2010-2016 Facebook, Inc. (http://www.facebook.com)     |
   | Copyright (c) 1997-2010 The PHP Group                                |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
*/

#include "hphp/runtime/ext/hh/ext_hh.h"

#include <sys/types.h>
#include <sys/stat.h>

#include "hphp/runtime/base/autoload-handler.h"
#include "hphp/runtime/base/container-functions.h"
#include "hphp/runtime/base/execution-context.h"
#include "hphp/runtime/base/unit-cache.h"
#include "hphp/runtime/ext/fb/ext_fb.h"
#include "hphp/runtime/vm/runtime.h"
#include "hphp/runtime/vm/vm-regs.h"

namespace HPHP {

//////////////////////////////////////////////////////////////////////

const StaticString
  s_86metadata("86metadata"),
  // The following are used in serialize_memoize_param(), and to not collide
  // with optimizations there, must be empty or start with a characther >=
  // FB_CS_MAX_CODE && < '0'
  s_empty(""),
  s_emptyArr("$array()"),
  s_emptyStr("$"),
  s_true("$true"),
  s_false("$false");

///////////////////////////////////////////////////////////////////////////////
bool HHVM_FUNCTION(autoload_set_paths,
                   const Variant& map,
                   const String& root) {
  if (map.isArray()) {
    return AutoloadHandler::s_instance->setMap(map.toCArrRef(), root);
  }
  if (!(map.isObject() && map.toObject()->isCollection())) {
    return false;
  }
  // Assume we have Map<string, Map<string, string>> - convert to
  // array<string, array<string, string>>
  //
  // Exception for 'failure' which should be a callable.
  auto as_array = map.toArray();
  for (auto it = as_array.begin(); !it.end(); it.next()) {
    if (it.second().isObject() && it.second().toObject()->isCollection()) {
      as_array.set(it.first(), it.second().toArray());
    }
  }
  return AutoloadHandler::s_instance->setMap(as_array, root);
}

bool HHVM_FUNCTION(could_include, const String& file) {
  return lookupUnit(file.get(), "", nullptr /* initial_opt */) != nullptr;
}

TypedValue HHVM_FUNCTION(serialize_memoize_param, TypedValue param) {
  // Memoize throws in the emitter if any function parameters are references, so
  // we can just assert that the param is cell here
  assertx(param.m_type != KindOfRef);
  auto const type = param.m_type;

  if (type == KindOfInt64) {
    return param;
  } else if (type == KindOfUninit || type == KindOfNull) {
    return make_tv<KindOfPersistentString>(s_empty.get());
  } else if (type == KindOfBoolean) {
    return make_tv<KindOfPersistentString>(param.m_data.num ?
                                 s_true.get() : s_false.get());
  } else if (isStringType(type)) {
    auto const str = param.m_data.pstr;
    if (str->empty()) {
      return make_tv<KindOfPersistentString>(s_emptyStr.get());
    } else if ((unsigned char)str->data()[0] > '9') {
      // If it's > '9' then its not strictly integer, and it doesn't
      // start with '$', so we know it won't collide with an integer
      // key or with any of our constants, so it's fine as is
      str->incRefCount();
      return param;
    }
  } else if (isContainer(param) && getContainerSize(param) == 0) {
    return make_tv<KindOfPersistentString>(s_emptyArr.get());
  }

  return tvReturn(
    fb_compact_serialize(tvAsCVarRef(&param),
                         FBCompactSerializeBehavior::MemoizeParam));
}

void HHVM_FUNCTION(set_frame_metadata, const Variant& metadata) {
  VMRegAnchor _;
  auto fp = vmfp();
  if (fp && fp->skipFrame()) fp = g_context->getPrevVMState(fp);
  if (UNLIKELY(!fp)) return;

  if (LIKELY(!(fp->func()->attrs() & AttrMayUseVV)) ||
      LIKELY(!fp->hasVarEnv())) {
    auto const local = fp->func()->lookupVarId(s_86metadata.get());
    if (LIKELY(local != kInvalidId)) {
      cellSet(*metadata.asCell(), *tvAssertCell(frame_local(fp, local)));
    } else {
      SystemLib::throwInvalidArgumentExceptionObject(
        "Unsupported dynamic call of set_frame_metadata()");
    }
  } else {
    fp->getVarEnv()->set(s_86metadata.get(), metadata.asTypedValue());
  }
}

static struct HHExtension final : Extension {
  HHExtension(): Extension("hh", NO_EXTENSION_VERSION_YET) { }
  void moduleInit() override {
    HHVM_NAMED_FE(HH\\autoload_set_paths, HHVM_FN(autoload_set_paths));
    HHVM_NAMED_FE(HH\\could_include, HHVM_FN(could_include));
    HHVM_NAMED_FE(HH\\serialize_memoize_param,
                  HHVM_FN(serialize_memoize_param));
    HHVM_NAMED_FE(HH\\set_frame_metadata, HHVM_FN(set_frame_metadata));
    loadSystemlib();
  }
} s_hh_extension;

static struct XHPExtension final : Extension {
  XHPExtension(): Extension("xhp", NO_EXTENSION_VERSION_YET) { }
  bool moduleEnabled() const override { return RuntimeOption::EnableXHP; }
} s_xhp_extension;

///////////////////////////////////////////////////////////////////////////////
}
