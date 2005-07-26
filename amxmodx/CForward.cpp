/* AMX Mod X
*
* by the AMX Mod X Development Team
*  originally developed by OLO
*
*
*  This program is free software; you can redistribute it and/or modify it
*  under the terms of the GNU General Public License as published by the
*  Free Software Foundation; either version 2 of the License, or (at
*  your option) any later version.
*
*  This program is distributed in the hope that it will be useful, but
*  WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
*  General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program; if not, write to the Free Software Foundation,
*  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*
*  In addition, as a special exception, the author gives permission to
*  link the code of this program with the Half-Life Game Engine ("HL
*  Engine") and Modified Game Libraries ("MODs") developed by Valve,
*  L.L.C ("Valve"). You must obey the GNU General Public License in all
*  respects for all of the code used other than the HL Engine and MODs
*  from Valve. If you modify this file, you may extend this exception
*  to your version of the file, but you are not obligated to do so. If
*  you do not wish to do so, delete this exception statement from your
*  version.
*/

#include "amxmodx.h"

void AMXAPI amxx_InvalidateTrace(AMX *amx);

CForward::CForward(const char *name, ForwardExecType et, int numParams, const ForwardParam *paramTypes)
{
	m_FuncName = name;
	m_ExecType = et;
	m_NumParams = numParams;
	memcpy((void *)m_ParamTypes, paramTypes, numParams * sizeof(ForwardParam));
	// find funcs
	int func;
	AMXForward *tmp = NULL;
	m_Funcs.clear();
	for (CPluginMngr::iterator iter = g_plugins.begin(); iter; ++iter)
	{
		if ((*iter).isValid() && amx_FindPublic((*iter).getAMX(), name, &func) == AMX_ERR_NONE)
		{
			AMXForward tmp;
			tmp.pPlugin = &(*iter);
			tmp.func = func;
			m_Funcs.push_back(tmp);
		}
	}
}

cell CForward::execute(cell *params, ForwardPreparedArray *preparedArrays)
{
	cell realParams[FORWARD_MAX_PARAMS];
	cell *physAddrs[FORWARD_MAX_PARAMS];

	const int STRINGEX_MAXLENGTH = 128;

	cell globRetVal = 0;

	unsigned int id = 0;

	AMXForwardList::iterator iter;

	for (iter = m_Funcs.begin(); iter != m_Funcs.end(); iter++)
	{
		if (iter->pPlugin->isExecutable(iter->func))
		{
			// Get debug info
			AMX *amx = (*iter).pPlugin->getAMX();
			AMX_DBGINFO *pInfo = (AMX_DBGINFO *)(amx->userdata[2]);
			if (pInfo)
				pInfo->error = AMX_ERR_NONE;
			// handle strings & arrays
			int i, ax=0;
			for (i = 0; i < m_NumParams; ++i)
			{
				if (m_ParamTypes[i] == FP_STRING || m_ParamTypes[i] == FP_STRINGEX)
				{
					cell *tmp;
					amx_Allot(iter->pPlugin->getAMX(),
						(m_ParamTypes[i] == FP_STRING) ? strlen(reinterpret_cast<const char*>(params[i]))+1 : STRINGEX_MAXLENGTH,
						&realParams[i], &tmp);
					amx_SetStringOld(tmp, (const char *)(params[i]), 0, 0);
					physAddrs[i] = tmp;
				}
				else if (m_ParamTypes[i] == FP_ARRAY)
				{
					cell *tmp;
					amx_Allot(amx, preparedArrays[params[i]].size,
						&realParams[i], &tmp);
					physAddrs[i] = tmp;
					if (preparedArrays[params[i]].type == Type_Cell)
					{
						memcpy(tmp, preparedArrays[params[i]].ptr, preparedArrays[params[i]].size * sizeof(cell));
					}
					else
					{
						char *data = (char*)preparedArrays[params[i]].ptr;
						for (unsigned int j = 0; j < preparedArrays[params[i]].size; ++j)
							*tmp++ = (static_cast<cell>(*data++)) & 0xFF;
					}
				}
				else
				{
					realParams[i] = params[i];
				}
			}
			//Push the parameters in reverse order.  Weird, unfriendly part of Small 3.0!
			for (i=m_NumParams-1; i>=0; i--)
			{
				amx_Push(amx, realParams[i]);
			}
			// exec
			cell retVal;
			int err = amx_Exec(amx, &retVal, iter->func);
			// log runtime error, if any
			if (err != AMX_ERR_NONE)
			{
				//Did something else set an error?
				if (pInfo && pInfo->error != AMX_ERR_NONE)
				{
					//we don't care, something else logged the error.
				} else {
					//nothing logged the error so spit it out anyway
					LogError(amx, err, "");
				}
			}
			amxx_InvalidateTrace(amx);
			amx->error = AMX_ERR_NONE;

			// cleanup strings & arrays
			for (i = 0; i < m_NumParams; ++i)
			{
				if (m_ParamTypes[i] == FP_STRING)
				{
					amx_Release(iter->pPlugin->getAMX(), realParams[i]);
				}
				else if (m_ParamTypes[i] == FP_STRINGEX)
				{
					// copy back
					amx_GetStringOld(reinterpret_cast<char*>(params[i]), physAddrs[i], 0);
					amx_Release(iter->pPlugin->getAMX(), realParams[i]);
				}
				else if (m_ParamTypes[i] == FP_ARRAY)
				{
					// copy back
					if (preparedArrays[params[i]].copyBack)
					{
						cell *tmp = physAddrs[i];
						if (preparedArrays[params[i]].type == Type_Cell)
						{
							memcpy(preparedArrays[params[i]].ptr, tmp, preparedArrays[params[i]].size * sizeof(cell));
						}
						else
						{
							char *data = (char*)preparedArrays[params[i]].ptr;
							for (unsigned int j = 0; j < preparedArrays[params[i]].size; ++j)
								*data++ = static_cast<char>(*tmp++ & 0xFF);
						}
					}
					amx_Release(iter->pPlugin->getAMX(), realParams[i]);
				}
			}

			// decide what to do (based on exectype and retval)
			switch (m_ExecType)
			{
			case ET_IGNORE:
				break;
			case ET_STOP:
				if (retVal > 0)
					return retVal;
			case ET_STOP2:
				if (retVal == 1)
					return 1;
				else if (retVal > globRetVal)
					globRetVal = retVal;
				break;
			case ET_CONTINUE:
				if (retVal > globRetVal)
					globRetVal = retVal;
				break;
			}
		}
	}
	return globRetVal;
}

void CSPForward::Set(int func, AMX *amx, int numParams, const ForwardParam *paramTypes)
{
	m_Func = func;
	m_Amx = amx;
	m_NumParams = numParams;
	memcpy((void *)m_ParamTypes, paramTypes, numParams * sizeof(ForwardParam));
	m_HasFunc = true;
	isFree = false;
}

void CSPForward::Set(const char *funcName, AMX *amx, int numParams, const ForwardParam *paramTypes)
{
	m_Amx = amx;
	m_NumParams = numParams;
	memcpy((void *)m_ParamTypes, paramTypes, numParams * sizeof(ForwardParam));
	m_HasFunc = (amx_FindPublic(amx, funcName, &m_Func) == AMX_ERR_NONE);
	isFree = false;
}

cell CSPForward::execute(cell *params, ForwardPreparedArray *preparedArrays)
{
	if (isFree)
		return 0;
	
	const int STRINGEX_MAXLENGTH = 128;

	cell realParams[FORWARD_MAX_PARAMS];
	cell *physAddrs[FORWARD_MAX_PARAMS];

	if (!m_HasFunc)
		return 0;
	
	CPluginMngr::CPlugin *pPlugin = g_plugins.findPluginFast(m_Amx);
	if (!pPlugin->isExecutable(m_Func))
		return 0;

	AMX_DBGINFO *pInfo = (AMX_DBGINFO *)(m_Amx->userdata[2]);
	if (pInfo)
		pInfo->error = AMX_ERR_NONE;

	// handle strings & arrays
	int i;
	for (i = 0; i < m_NumParams; ++i)
	{
		if (m_ParamTypes[i] == FP_STRING || m_ParamTypes[i] == FP_STRINGEX)
		{
			cell *tmp;
			amx_Allot(m_Amx,
				(m_ParamTypes[i] == FP_STRING) ? strlen(reinterpret_cast<const char*>(params[i]))+1 : STRINGEX_MAXLENGTH,
				&realParams[i], &tmp);
			amx_SetStringOld(tmp, (const char *)(params[i]), 0, 0);
			physAddrs[i] = tmp;
		}
		else if (m_ParamTypes[i] == FP_ARRAY)
		{
			cell *tmp;
			amx_Allot(m_Amx, preparedArrays[params[i]].size,
				&realParams[i], &tmp);
			physAddrs[i] = tmp;
			if (preparedArrays[params[i]].type == Type_Cell)
			{
				memcpy(tmp, preparedArrays[params[i]].ptr, preparedArrays[params[i]].size * sizeof(cell));
			}
			else
			{
				char *data = (char*)preparedArrays[params[i]].ptr;
				for (unsigned int j = 0; j < preparedArrays[params[i]].size; ++j)
					*tmp++ = (static_cast<cell>(*data++)) & 0xFF;
			}
		}
		else
		{
			realParams[i] = params[i];
		}
	}
	for (i=m_NumParams-1; i>=0; i--)
		amx_Push(m_Amx, realParams[i]);
	// exec
	cell retVal;
	int err = amx_Exec(m_Amx, &retVal, m_Func);
	if (err != AMX_ERR_NONE)
	{
		//Did something else set an error?
		if (pInfo && pInfo->error != AMX_ERR_NONE)
		{
			//we don't care, something else logged the error.
		} else {
			//nothing logged the error so spit it out anyway
			LogError(m_Amx, err, "");
		}
	}
	amxx_InvalidateTrace(m_Amx);
	m_Amx->error = AMX_ERR_NONE;

	// cleanup strings & arrays
	for (i = 0; i < m_NumParams; ++i)
	{
		if (m_ParamTypes[i] == FP_STRING)
		{
			amx_Release(m_Amx, realParams[i]);
		}
		else if (m_ParamTypes[i] == FP_STRINGEX)
		{
			// copy back
			amx_GetStringOld(reinterpret_cast<char*>(params[i]), physAddrs[i], 0);
			amx_Release(m_Amx, realParams[i]);
		}
		else if (m_ParamTypes[i] == FP_ARRAY)
		{
			// copy back
			if (preparedArrays[params[i]].copyBack)
			{
				cell *tmp = physAddrs[i];
				if (preparedArrays[params[i]].type == Type_Cell)
				{
					memcpy(preparedArrays[params[i]].ptr, tmp, preparedArrays[params[i]].size * sizeof(cell));
				}
				else
				{
					char *data = (char*)preparedArrays[params[i]].ptr;
					for (unsigned int j = 0; j < preparedArrays[params[i]].size; ++j)
						*data++ = static_cast<char>(*tmp++ & 0xFF);
				}
			}
			amx_Release(m_Amx, realParams[i]);
		}
	}

	return retVal;
}

int CForwardMngr::registerForward(const char *funcName, ForwardExecType et, int numParams, const ForwardParam * paramTypes)
{
	int retVal = m_Forwards.size() << 1;
	CForward *tmp = new CForward(funcName, et, numParams, paramTypes);
	if (!tmp)
		return -1;				// should be invalid
	m_Forwards.push_back(tmp);
	return retVal;
}

int CForwardMngr::registerSPForward(int func, AMX *amx, int numParams, const ForwardParam *paramTypes)
{
	int retVal = -1;
	CSPForward *pForward;
	if (!m_FreeSPForwards.empty())
	{
		retVal = m_FreeSPForwards.front();
		pForward = m_SPForwards[retVal >> 1];
		pForward->Set(func, amx, numParams, paramTypes);
		if (pForward->getFuncsNum() == 0)
			return -1;
		m_FreeSPForwards.pop();
	}
	else
	{
		retVal = (m_SPForwards.size() << 1) | 1;
		pForward = new CSPForward();
		if (!pForward)
			return -1;
		pForward->Set(func, amx, numParams, paramTypes);
		if (pForward->getFuncsNum() == 0)
		{
			return -1;
			delete pForward;
		}
					 
		m_SPForwards.push_back(pForward);
	}
	return retVal;
}

int CForwardMngr::registerSPForward(const char *funcName, AMX *amx, int numParams, const ForwardParam *paramTypes)
{
	int retVal = (m_SPForwards.size() << 1) | 1;
	CSPForward *pForward;
	if (!m_FreeSPForwards.empty())
	{
		retVal = m_FreeSPForwards.front();
		pForward = m_SPForwards[retVal>>1];		// >>1 because unregisterSPForward pushes the id which contains the sp flag
		pForward->Set(funcName, amx, numParams, paramTypes);
		if (pForward->getFuncsNum() == 0)
			return -1;
		m_FreeSPForwards.pop();
	}
	else
	{
		pForward = new CSPForward();
		if (!pForward)
			return -1;
		pForward->Set(funcName, amx, numParams, paramTypes);
		if (pForward->getFuncsNum() == 0)
		{
			delete pForward;
			return -1;
		}
		m_SPForwards.push_back(pForward);
	}
	return retVal;
}

bool CForwardMngr::isIdValid(int id) const
{
	return (id >= 0) && ((id & 1) ?
		(static_cast<size_t>(id >> 1) < m_SPForwards.size()) :
		(static_cast<size_t>(id >> 1) < m_Forwards.size()));
}

cell CForwardMngr::executeForwards(int id, cell *params)
{
	int retVal = (id & 1) ? m_SPForwards[id >> 1]->execute(params, m_TmpArrays) : 
		m_Forwards[id >> 1]->execute(params, m_TmpArrays);
	m_TmpArraysNum = 0;
	return retVal;
}

int CForwardMngr::getParamsNum(int id) const
{
	return (id & 1) ? m_SPForwards[id >> 1]->getParamsNum() :
		m_Forwards[id >> 1]->getParamsNum();
}

ForwardParam CForwardMngr::getParamType(int id, int paramNum) const
{
	return (id & 1) ? m_SPForwards[id >> 1]->getParamType(paramNum) :
		m_Forwards[id >> 1]->getParamType(paramNum);
}

void CForwardMngr::clear()
{
	for (ForwardVec::iterator iter = m_Forwards.begin(); iter != m_Forwards.end(); ++iter)
	{
		delete *iter;
	}
	SPForwardVec::iterator spIter;
	for (spIter = m_SPForwards.begin(); spIter != m_SPForwards.end(); ++spIter)
	{
		delete (*spIter);
	}

	m_Forwards.clear();
	m_SPForwards.clear();
	while (!m_FreeSPForwards.empty())
		m_FreeSPForwards.pop();
	m_TmpArraysNum = 0;
}

bool CForwardMngr::isSPForward(int id) const
{
	return ((id & 1) == 0) ? false : true;
}

void CForwardMngr::unregisterSPForward(int id)
{
	//make sure the id is valid
	if ( !isIdValid(id) || m_SPForwards.at(id >> 1)->isFree )
		return;

	m_SPForwards.at(id >> 1)->isFree = true;

	m_FreeSPForwards.push(id);
}

int registerForward(const char *funcName, ForwardExecType et, ...)
{
	int curParam = 0;
	va_list argptr;
	va_start(argptr, et);
	ForwardParam params[FORWARD_MAX_PARAMS];
	ForwardParam tmp;
	while (true)
	{
		if (curParam == FORWARD_MAX_PARAMS)
			break;
		tmp = (ForwardParam)va_arg(argptr, int);
		if (tmp == FP_DONE)
			break;
		params[curParam] = tmp;
		++curParam;
	}
	va_end(argptr);
	return g_forwards.registerForward(funcName, et, curParam, params);
}

int registerSPForwardByName(AMX *amx, const char *funcName, ...)
{
	int curParam = 0;
	va_list argptr;
	va_start(argptr, funcName);
	ForwardParam params[FORWARD_MAX_PARAMS];
	ForwardParam tmp;
	while (true)
	{
		if (curParam == FORWARD_MAX_PARAMS)
			break;
		tmp = (ForwardParam)va_arg(argptr, int);
		if (tmp == FP_DONE)
			break;
		params[curParam] = tmp;
		++curParam;
	}
	va_end(argptr);
	return g_forwards.registerSPForward(funcName, amx, curParam, params);
}

int registerSPForward(AMX *amx, int func, ...)
{
	int curParam = 0;
	va_list argptr;
	va_start(argptr, func);
	ForwardParam params[FORWARD_MAX_PARAMS];
	ForwardParam tmp;
	while (true)
	{
		if (curParam == FORWARD_MAX_PARAMS)
			break;
		tmp = (ForwardParam)va_arg(argptr, int);
		if (tmp == FP_DONE)
			break;
		params[curParam] = tmp;
		++curParam;
	}
	va_end(argptr);
	return g_forwards.registerSPForward(func, amx, curParam, params);
}

cell executeForwards(int id, ...)
{
	if (!g_forwards.isIdValid(id))
		return -1;

	cell params[FORWARD_MAX_PARAMS];
	int paramsNum = g_forwards.getParamsNum(id);
	va_list argptr;
	va_start(argptr, id);
	for (int i = 0; i < paramsNum && i < FORWARD_MAX_PARAMS; ++i)
	{
		if (g_forwards.getParamType(id, i) == FP_FLOAT)
		{
			REAL tmp = (REAL)va_arg(argptr, double);			// floats get converted to doubles
			params[i] = *(cell*)&tmp;
		}
		else
			params[i] = (cell)va_arg(argptr, cell);
	}
	va_end(argptr);
	return g_forwards.executeForwards(id, params);
}

cell CForwardMngr::prepareArray(void *ptr, unsigned int size, ForwardArrayElemType type, bool copyBack)
{
	if (m_TmpArraysNum >= FORWARD_MAX_PARAMS)
	{
		#ifdef MEMORY_TEST
		m_validateAllAllocUnits();
		#endif // MEMORY_TEST
		AMXXLOG_Log("[AMXX] Forwards with more than 32 parameters are not supported (tried to prepare array # %d).", m_TmpArraysNum + 1);
		m_TmpArraysNum = 0;
		return -1;
	}
	m_TmpArrays[m_TmpArraysNum].ptr = ptr;
	m_TmpArrays[m_TmpArraysNum].size = size;
	m_TmpArrays[m_TmpArraysNum].type = type;
	m_TmpArrays[m_TmpArraysNum].copyBack = copyBack;

	return m_TmpArraysNum++;
}

cell prepareCellArray(cell *ptr, unsigned int size, bool copyBack)
{
	return g_forwards.prepareArray((void*)ptr, size, Type_Cell, copyBack);
}

cell prepareCharArray(char *ptr, unsigned int size, bool copyBack)
{
	return g_forwards.prepareArray((void*)ptr, size, Type_Char, copyBack);
}

void unregisterSPForward(int id)
{
	g_forwards.unregisterSPForward(id);
}
