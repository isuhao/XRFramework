#pragma once
#include <tinyxml2.h>
#include <memory>
#include <vector>
#include "ISettingCallback.h"
#include "../XRThreads/CriticalSection.h"
#include "../XRThreads/SingleLock.h"

class CSettingManager;

enum class SettingMode {
	SM_VALUE,
	SM_LIST
};

enum class SettingType {
	ST_INT,
	ST_BOOL,
	ST_STRING,
	ST_FLOAT,
	ST_UKNOWN
};

using namespace tinyxml2;

class CSetting : protected ISettingCallback  {
public:
	CSetting(const std::string &id, CSettingManager *settingsManager = nullptr);
	virtual ~CSetting() {};

	virtual bool Deserialize(const XMLNode *node, bool update = false) { DeserializeIdentification(node, m_id); return true; };

	const std::string& GetID() const { return m_id; }
	virtual SettingType GetType() { XR::CSingleLock lck(m_critical); return m_type; }
	virtual void SetCallback(ISettingCallback* callback) { m_callback = callback; }
	virtual void Reset() {}

	virtual bool OnSettingChanging(const CSetting* setting);
	virtual void OnSettingChanged(const CSetting* setting);
	virtual bool FromString(const std::string &value) { return false; };
	virtual std::string ToString() { return ""; };
	virtual bool Serialize(XMLNode *node) = 0;

	static bool DeserializeIdentification(const XMLNode *node, std::string &identification);

	static SettingType GetTypeFromString(const std::string str);
	static std::string GetStringFromType(const SettingType str);
	
protected:
	static bool GetInt(const XMLNode* pRootNode, const char* strTag, int& iIntValue);
	static bool GetBool(const XMLNode* pRootNode, const char* strTag, bool& bBoolValue);
	static bool GetString(const XMLNode* pRootNode, const char* strTag, std::string& strStringValue);
	static bool GetFloat(const XMLNode* pRootNode, const char* strTag, float& fFloatValue);

	static void SetString(XMLNode* pRootNode, const char *strTag, const std::string& strValue);
	static void SetInt(XMLNode* pRootNode, const char *strTag, int value);
	static void SetFloat(XMLNode* pRootNode, const char *strTag, float value);
	static void SetBoolean(XMLNode* pRootNode, const char *strTag, bool value);


protected:
	std::string m_id;
	CSettingManager* m_settingmanager;
	SettingType m_type;
	bool m_changed;
	bool m_bList;
	ISettingCallback* m_callback;
	XR::CCriticalSection m_critical;
};

typedef std::shared_ptr<CSetting> SettingPtr;

typedef std::vector<CSetting *> SettingList;
typedef std::vector<SettingPtr> SettingPtrList;
