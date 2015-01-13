#pragma once

//Forward declarations
class CSettingManager;
class CSetting;
enum class SettingType;

 /*!*\class ISettingCreator
 * \brief Interface for Setting Creation
 * \author XoRdi
 * \date stycze� 2015*/
class ISettingCreator
{
public:
	virtual CSetting* CreateSetting(SettingType settingtype, std::string &settingId, CSettingManager* settingManaget = nullptr) = 0;
};

