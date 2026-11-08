# SPDX-License-Identifier: LGPL-2.1-or-later
# Harden official REAPER-service paths now that real callbacks may be supplied.
# Vendored upstream remains byte-for-byte unchanged.
set(_old_pin_set [=[  unsigned int mapping=double_to_unsigned_int_mask(*parms[4]);
  return (EEL_F)get_set_pin_mapping(p->m_hostctx,
    (int)*parms[0], (int)*parms[1], (int)*parms[2], double_to_unsigned_int_mask(*parms[3]), &mapping);]=])
set(_new_pin_set [=[  unsigned int mapping=double_to_unsigned_int_mask(*parms[4]);
  extern bool runtime_set_pin_mapping(SX_Instance*,int,int,int,unsigned int,unsigned int,unsigned int*);
  unsigned int result=0;
  if(runtime_set_pin_mapping(p,(int)*parms[0],(int)*parms[1],(int)*parms[2],
      double_to_unsigned_int_mask(*parms[3]),mapping,&result)) return (EEL_F)result;
  return (EEL_F)get_set_pin_mapping(p->m_hostctx,
    (int)*parms[0], (int)*parms[1], (int)*parms[2], double_to_unsigned_int_mask(*parms[3]), &mapping);]=])
string(FIND "${_miscfunc}" "${_old_pin_set}" _found)
if(_found LESS 0)
  message(FATAL_ERROR "Upstream pin mapping setter changed; review REAPER service patch")
endif()
string(REPLACE "${_old_pin_set}" "${_new_pin_set}" _miscfunc "${_miscfunc}")
string(REPLACE [=[  if (!p || !p->m_hostctx || !fxGetSetHostNumChan)
  {
    return (EEL_F)0;
  }
  return (EEL_F)fxGetSetPinmapperFlags]=] [=[  if (!p || !p->m_hostctx || !fxGetSetPinmapperFlags)
  {
    return (EEL_F)0;
  }
  return (EEL_F)fxGetSetPinmapperFlags]=] _miscfunc "${_miscfunc}")
string(REPLACE [=[EEL_F NSEEL_CGEN_CALL _set_pinmapper_flags(SX_Instance *p, EEL_F *v)
{
  if (!p || !p->m_hostctx || !fxGetSetHostNumChan ||]=] [=[EEL_F NSEEL_CGEN_CALL _set_pinmapper_flags(SX_Instance *p, EEL_F *v)
{
  if (!p || !p->m_hostctx || !fxGetSetPinmapperFlags ||]=] _miscfunc "${_miscfunc}")
set(_old_validate [=[static bool validate_buffers(int baseoffs, int len, int nch, int planar_pitch)
{
  if (planar_pitch == 0)
    return baseoffs >= 0 && baseoffs+len*nch <= NSEEL_RAM_BLOCKS*NSEEL_RAM_ITEMSPERBLOCK;

  for (int x = 0; x < nch; x ++)
  {
    if (baseoffs < 0 || baseoffs+len > NSEEL_RAM_BLOCKS*NSEEL_RAM_ITEMSPERBLOCK) return false;
    baseoffs += planar_pitch;
  }

  return true;
}]=])
set(_new_validate [=[static bool validate_buffers(int baseoffs, int len, int nch, int planar_pitch)
{
  const WDL_INT64 limit=(WDL_INT64)NSEEL_RAM_BLOCKS*NSEEL_RAM_ITEMSPERBLOCK;
  if (baseoffs < 0 || len < 1 || nch < 1 || nch > REAPER_MAX_CHANNELS) return false;
  if (planar_pitch == 0) return (WDL_INT64)baseoffs+(WDL_INT64)len*nch <= limit;
  for(int x=0;x<nch;++x) {
    const WDL_INT64 pos=(WDL_INT64)baseoffs+(WDL_INT64)planar_pitch*x;
    if(pos<0 || pos+len>limit) return false;
  }
  return true;
}]=])
string(FIND "${_miscfunc}" "${_old_validate}" _found)
if(_found LESS 0)
  message(FATAL_ERROR "Upstream export buffer validator changed; review REAPER service patch")
endif()
string(REPLACE "${_old_validate}" "${_new_validate}" _miscfunc "${_miscfunc}")
string(REPLACE [=[EEL_F NSEEL_CGEN_CALL _export_buffer_to_project(void *opaque, INT_PTR np, EEL_F **parms)]=] [=[extern bool runtime_export_begin(SX_Instance*,void**);
extern void runtime_export_end(void*);
EEL_F NSEEL_CGEN_CALL _export_buffer_to_project(void *opaque, INT_PTR np, EEL_F **parms)]=] _miscfunc "${_miscfunc}")
string(REPLACE [=[  double rv=-1.0;

  const int baseoffs]=] [=[  double rv=-1.0;
  // Check before floating->integer casts and before any project/file operation.
  for(int i=0;i<np && i<8;++i) {
    if(!parms[i] || !std::isfinite(parms[i][0]) || parms[i][0]<-2147483647.0 || parms[i][0]>2147483646.0) return -1.0;
  }
  if(parms[0][0]<0 || parms[1][0]<1 || parms[2][0]<1 || parms[3][0]<1) return -1.0;

  const int baseoffs]=] _miscfunc "${_miscfunc}")
# Move cursor/tempo actions after a complete sink write, immediately before insertion.
string(FIND "${_miscfunc}" "  if (flags & 0x10000)" _actions_begin)
string(FIND "${_miscfunc}" "  if (nch >= 1 && nch <= REAPER_MAX_CHANNELS && len > 0 &&" _actions_end)
if(_actions_begin LESS 0 OR _actions_end LESS _actions_begin)
  message(FATAL_ERROR "Upstream export action block changed; review REAPER service patch")
endif()
math(EXPR _actions_len "${_actions_end}-${_actions_begin}")
string(SUBSTRING "${_miscfunc}" ${_actions_begin} ${_actions_len} _export_actions)
set(_export_scope [=[  if(!Plugin_FindNewFileName || !PCM_Sink_Create || !InsertMedia || sr<1 || sr>=(1<<20) ||
      !validate_buffers(baseoffs,len,nch,planar_pitch)) return -1.0;
  void* exportToken=NULL;
  if(!runtime_export_begin(epctx,&exportToken)) return -1.0;
  struct ExportScope { void* token; ~ExportScope(){runtime_export_end(token);} } scope={exportToken};

]=])
string(REPLACE "${_export_actions}" "${_export_scope}" _miscfunc "${_miscfunc}")
set(_old_finish [=[          delete sink;

          if (InsertMedia)
          {
            InsertMedia(tmp,(trackidx<<16) | 512 | (flags&0x1fc /*only 4-256 allowed through*/));
          }
          rv=0.0;]=])
set(_new_finish "          const bool complete=remaining==0 && sink->GetLength()+0.5/sr >= (double)len/sr && sink->GetFileSize()>0;\n          delete sink;\n          if(complete) {\n${_export_actions}\n            rv=InsertMedia(tmp,(trackidx<<16) | 512 | (flags&0x1fc)) ? 0.0 : -1.0;\n          }")
string(FIND "${_miscfunc}" "${_old_finish}" _found)
if(_found LESS 0)
  message(FATAL_ERROR "Upstream export completion changed; review REAPER service patch")
endif()
string(REPLACE "${_old_finish}" "${_new_finish}" _miscfunc "${_miscfunc}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/jsfx_miscfunc.cpp" "#include <cmath>\n${_miscfunc}")
