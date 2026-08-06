#include "CamPath.h"

#include <stdio.h>
#include <algorithm>
#include <string>
#include <vector>

#define _USE_MATH_DEFINES
#include <math.h>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

bool CamPath::DoubleInterp_FromString(char const * value, DoubleInterp & outValue)
{
	if(!_stricmp(value,"default"))
	{
		outValue = DI_DEFAULT;
		return true;
	}
	else
	if(!_stricmp(value,"linear"))
	{
		outValue = DI_LINEAR;
		return true;
	}
	else
	if(!_stricmp(value,"cubic"))
	{
		outValue = DI_CUBIC;
		return true;
	}

	return false;
}

char const * CamPath::DoubleInterp_ToString(DoubleInterp value)
{
	switch(value)
	{
	case DI_DEFAULT:
		return "default";
	case DI_LINEAR:
		return "linear";
	case DI_CUBIC:
		return "cubic";
	}

	return "[unkown]";
}

bool CamPath::QuaternionInterp_FromString(char const * value, QuaternionInterp & outValue)
{
	if(!_stricmp(value,"default"))
	{
		outValue = QI_DEFAULT;
		return true;
	}
	else
	if(!_stricmp(value,"sLinear"))
	{
		outValue = QI_SLINEAR;
		return true;
	}
	else
	if(!_stricmp(value,"sCubic"))
	{
		outValue = QI_SCUBIC;
		return true;
	}

	return false;
}

char const * CamPath::QuaternionInterp_ToString(QuaternionInterp value)
{
	switch(value)
	{
	case QI_DEFAULT:
		return "default";
	case QI_SLINEAR:
		return "sLinear";
	case QI_SCUBIC:
		return "sCubic";
	}

	return "[unkown]";
}

CamPathValue::CamPathValue()
: X(0.0), Y(0.0), Z(0.0), R(), Fov(90.0), Selected(false)
{
}

CamPathValue::CamPathValue(double x, double y, double z, double pitch, double yaw, double roll, double fov)
: X(x)
, Y(y)
, Z(z)
, R(Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(pitch,yaw,roll))))
, Fov(fov)
, Selected(false)
{
}

CamPathValue::CamPathValue(double x, double y, double z, double q_w, double q_x, double q_y, double q_z, double fov, bool selected)
: X(x), Y(y), Z(z), R(Quaternion(q_w,q_x,q_y,q_z)), Fov(fov), Selected(selected) {
}

CamPathIterator::CamPathIterator(CInterpolationMap<CamPathValue>::const_iterator & it) : wrapped(it)
{
}

double CamPathIterator::GetTime() const
{
	return wrapped->first;
}

CamPathValue CamPathIterator::GetValue() const
{
	return wrapped->second;
}

CamPathIterator& CamPathIterator::operator ++ ()
{
	wrapped++;
	return *this;
}

bool CamPathIterator::operator == (CamPathIterator const &it) const
{
	return wrapped == it.wrapped;
}

bool CamPathIterator::operator != (CamPathIterator const &it) const
{
	return !(*this == it);
}

// CamPath /////////////////////////////////////////////////////////////////////

CamPath::CamPath()
: m_Offset(0)
, m_Enabled(false)
, m_PositionInterpMethod(DI_DEFAULT)
, m_RotationInterpMethod(QI_DEFAULT)
, m_FovInterpMethod(DI_DEFAULT)
, m_XView(&m_Map, XSelector)
, m_YView(&m_Map, YSelector)
, m_ZView(&m_Map, ZSelector)
, m_RView(&m_Map, RSelector)
, m_FovView(&m_Map, FovSelector)
, m_SelectedView(&m_Map, SelectedSelector)
{
	m_OnChangedIt = m_OnChanged.end();

	m_XInterp = new CCubicDoubleInterpolation<CamPathValue>(&m_XView);
	m_YInterp = new CCubicDoubleInterpolation<CamPathValue>(&m_YView);
	m_ZInterp = new CCubicDoubleInterpolation<CamPathValue>(&m_ZView);
	m_RInterp = new CSCubicQuaternionInterpolation<CamPathValue>(&m_RView);
	m_FovInterp = new CCubicDoubleInterpolation<CamPathValue>(&m_FovView);
	m_SelectedInterp = new CBoolAndInterpolation<CamPathValue>(&m_SelectedView);
}

CamPath::~CamPath()
{
	m_Map.clear();

	delete m_SelectedInterp;
	delete m_FovInterp;
	delete m_RInterp;
	delete m_ZInterp;
	delete m_YInterp;
	delete m_XInterp;
}

void CamPath::DoInterpolationMapChangedAll(void)
{
	m_XInterp->InterpolationMapChanged();
	m_YInterp->InterpolationMapChanged();
	m_ZInterp->InterpolationMapChanged();
	m_RInterp->InterpolationMapChanged();
	m_FovInterp->InterpolationMapChanged();
	m_SelectedInterp->InterpolationMapChanged();
}

void CamPath::Enabled_set(bool enable)
{
	m_Enabled = enable;
}

bool CamPath::Enabled_get(void) const
{
	return m_Enabled;
}

bool CamPath::GetHold(void) const
{
	return m_Hold;
}

void CamPath::SetHold(bool value)
{
	m_Hold = value;
}

void CamPath::PositionInterpMethod_set(DoubleInterp value)
{
	delete m_XInterp;
	delete m_YInterp;
	delete m_ZInterp;

	m_PositionInterpMethod = value;

	switch(value)
	{
	case DI_LINEAR:
		m_XInterp = new CLinearDoubleInterpolation<CamPathValue>(&m_XView);
		m_YInterp = new CLinearDoubleInterpolation<CamPathValue>(&m_YView);
		m_ZInterp = new CLinearDoubleInterpolation<CamPathValue>(&m_ZView);
		break;
	default:
		m_XInterp = new CCubicDoubleInterpolation<CamPathValue>(&m_XView);
		m_YInterp = new CCubicDoubleInterpolation<CamPathValue>(&m_YView);
		m_ZInterp = new CCubicDoubleInterpolation<CamPathValue>(&m_ZView);
		break;
	}

	Changed();
}

CamPath::DoubleInterp CamPath::PositionInterpMethod_get(void) const
{
	return m_PositionInterpMethod;
}

void CamPath::RotationInterpMethod_set(QuaternionInterp value)
{
	delete m_RInterp;

	m_RotationInterpMethod = value;

	switch(value)
	{
	case QI_SLINEAR:
		m_RInterp = new CSLinearQuaternionInterpolation<CamPathValue>(&m_RView);
		break;
	default:
		m_RInterp = new CSCubicQuaternionInterpolation<CamPathValue>(&m_RView);
		break;
	}

	Changed();
}

CamPath::QuaternionInterp CamPath::RotationInterpMethod_get(void) const
{
	return m_RotationInterpMethod;
}

void CamPath::FovInterpMethod_set(DoubleInterp value)
{
	delete m_FovInterp;

	m_FovInterpMethod = value;

	switch(value)
	{
	case DI_LINEAR:
		m_FovInterp = new CLinearDoubleInterpolation<CamPathValue>(&m_FovView);
		break;
	default:
		m_FovInterp = new CCubicDoubleInterpolation<CamPathValue>(&m_FovView);
		break;
	}

	Changed();
}

CamPath::DoubleInterp CamPath::FovInterpMethod_get(void) const
{
	return m_FovInterpMethod;
}

void CamPath::Add(double time, const CamPathValue & value)
{
	m_Map[time] = value;
	DoInterpolationMapChangedAll();
	Changed();
}

void CamPath::Changed()
{
	for(m_OnChangedIt = m_OnChanged.begin(); m_OnChangedIt != m_OnChanged.end(); m_OnChangedIt++) {
		m_OnChangedIt->Notify();
		if(m_OnChangedIt == m_OnChanged.end()) break;
	}
}

void CamPath::Remove(double time)
{
	m_Map.erase(time);
	DoInterpolationMapChangedAll();
	Changed();
}

void CamPath::Clear()
{
	bool selectAll = true;

	CInterpolationMap<CamPathValue>::iterator last = m_Map.end();
	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end();)
	{
		CInterpolationMap<CamPathValue>::iterator itNext = it;
		++itNext;

		if(it->second.Selected)
		{
			selectAll = false;
			m_Map.erase(it);
		}

		it = itNext;
	}

	if(selectAll) m_Map.clear();

	m_Offset = 0;

	DoInterpolationMapChangedAll();
	Changed();
}

size_t CamPath::GetSize() const
{
	return m_Map.size();
}

CamPathIterator CamPath::GetBegin()
{
	return CamPathIterator(m_Map.begin());
}

CamPathIterator CamPath::GetEnd()
{
	return CamPathIterator(m_Map.end());
}

double CamPath::GetLowerBound() const
{
	return m_Map.cbegin()->first;
}

double CamPath::GetUpperBound() const
{
	return (--m_Map.cend())->first;
}

bool CamPath::CanEval(void) const
{
	return
		m_XInterp->CanEval()
		&& m_YInterp->CanEval()
		&& m_ZInterp->CanEval()
		&& m_RInterp->CanEval()
		&& m_FovInterp->CanEval()
		&& m_SelectedInterp->CanEval();
}

CamPathValue CamPath::Eval(double t)
{
	CamPathValue val;
	
	val.X = m_XInterp->Eval(t);
	val.Y = m_YInterp->Eval(t);
	val.Z = m_ZInterp->Eval(t);
	val.R = m_RInterp->Eval(t);
	val.Fov = m_FovInterp->Eval(t);
	val.Selected = m_SelectedInterp->Eval(t);

	return val;
}

namespace
{
	bool XmlHasAttribute(const char * begin, const char * end, const char * name)
	{
		if (!begin || !end || !name || begin >= end)
			return false;

		std::string needle(" ");
		needle += name;
		const char * found = strstr(begin, needle.c_str());
		if (!found || end <= found)
			return false;
		found += needle.size();
		while (found < end && (' ' == *found || '\t' == *found || '\r' == *found || '\n' == *found))
			++found;
		return found == end || '=' == *found || '>' == *found || '/' == *found;
	}

	bool XmlReadAttribute(const char * begin, const char * end, const char * name, std::string & value)
	{
		value.clear();
		if (!begin || !end || !name || begin >= end)
			return false;

		std::string needle(name);
		needle += "=\"";
		const char * found = strstr(begin, needle.c_str());
		if (!found || end <= found)
			return false;
		found += needle.size();
		const char * close = strchr(found, '"');
		if (!close || end < close)
			return false;
		value.assign(found, close);
		return true;
	}

	double XmlReadDouble(const char * begin, const char * end, const char * name, double fallback)
	{
		std::string value;
		return XmlReadAttribute(begin, end, name, value) ? atof(value.c_str()) : fallback;
	}
}

bool CamPath::Save(wchar_t const * fileName)
{
	FILE * file = 0;
	_wfopen_s(&file, fileName, L"wb");
	if (!file)
		return false;

	fprintf(file, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<campath");
	if(DI_DEFAULT != m_PositionInterpMethod)
		fprintf(file, " positionInterp=\"%s\"", DoubleInterp_ToString(m_PositionInterpMethod));
	if(QI_DEFAULT != m_RotationInterpMethod)
		fprintf(file, " rotationInterp=\"%s\"", QuaternionInterp_ToString(m_RotationInterpMethod));
	if(DI_DEFAULT != m_FovInterpMethod)
		fprintf(file, " fovInterp=\"%s\"", DoubleInterp_ToString(m_FovInterpMethod));
	if (m_Offset)
		fprintf(file, " offset=\"%.17g\"", m_Offset);
	if (m_Hold)
		fprintf(file, " hold");
	fprintf(file,
		">\n<points>\n"
		"<!--Points are in Quake coordinates, meaning x=forward, y=left, z=up and rotation order is first rx, then ry and lastly rz.\n"
		"Rotation direction follows the right-hand grip rule.\n"
		"rx (roll), ry (pitch), rz(yaw) are the Euler angles in degrees.\n"
		"qw, qx, qy, qz are the quaternion values.\n"
		"When read it is sufficient that either rx, ry, rz OR qw, qx, qy, qz are present.\n"
		"If both are present then qw, qx, qy, qz take precedence.-->\n");

	for(CamPathIterator it = GetBegin(); it != GetEnd(); ++it)
	{
		double time = it.GetTime();
		CamPathValue val = it.GetValue();
		QEulerAngles ang = val.R.ToQREulerAngles().ToQEulerAngles();
		fprintf(file,
			"<p t=\"%.17g\" x=\"%.17g\" y=\"%.17g\" z=\"%.17g\" fov=\"%.17g\" "
			"rx=\"%.17g\" ry=\"%.17g\" rz=\"%.17g\" "
			"qw=\"%.17g\" qx=\"%.17g\" qy=\"%.17g\" qz=\"%.17g\"%s />\n",
			time, val.X, val.Y, val.Z, val.Fov,
			ang.Roll, ang.Pitch, ang.Yaw,
			val.R.W, val.R.X, val.R.Y, val.R.Z,
			val.Selected ? " selected" : "");
	}

	const bool ok = 0 <= fprintf(file, "</points>\n</campath>\n") && !ferror(file);
	fclose(file);
	return ok;
}

bool CamPath::Load(wchar_t const * fileName)
{
	FILE * pFile = 0;
	_wfopen_s(&pFile, fileName, L"rb");
	if(!pFile)
		return false;

	fseek(pFile, 0, SEEK_END);
	long fileSizeLong = ftell(pFile);
	rewind(pFile);
	if (fileSizeLong <= 0 || 16 * 1024 * 1024 < fileSizeLong)
	{
		fclose(pFile);
		return false;
	}
	size_t fileSize = static_cast<size_t>(fileSizeLong);
	std::vector<char> data(fileSize + 1, 0);
	bool bOk = fileSize == fread(&data[0], sizeof(char), fileSize, pFile);
	fclose(pFile);
	if (!bOk)
		return false;

	const char * root = strstr(&data[0], "<campath");
	const char * rootEnd = root ? strchr(root, '>') : 0;
	if (!root || !rootEnd)
		return false;

	DoubleInterp positionInterp = DI_DEFAULT;
	QuaternionInterp rotationInterp = QI_DEFAULT;
	DoubleInterp fovInterp = DI_DEFAULT;
	std::string attribute;
	if (XmlReadAttribute(root, rootEnd, "positionInterp", attribute))
		DoubleInterp_FromString(attribute.c_str(), positionInterp);
	if (XmlReadAttribute(root, rootEnd, "rotationInterp", attribute))
		QuaternionInterp_FromString(attribute.c_str(), rotationInterp);
	if (XmlReadAttribute(root, rootEnd, "fovInterp", attribute))
		DoubleInterp_FromString(attribute.c_str(), fovInterp);

	CInterpolationMap<CamPathValue> loaded;
	const char * point = rootEnd;
	while ((point = strstr(point, "<p ")) != 0)
	{
		const char * pointEnd = strchr(point, '>');
		if (!pointEnd)
			return false;
		std::string timeText;
		if (!XmlReadAttribute(point, pointEnd, "t", timeText))
		{
			point = pointEnd + 1;
			continue;
		}
		const double time = atof(timeText.c_str());
		CamPathValue value;
		value.X = XmlReadDouble(point, pointEnd, "x", 0.0);
		value.Y = XmlReadDouble(point, pointEnd, "y", 0.0);
		value.Z = XmlReadDouble(point, pointEnd, "z", 0.0);
		value.Fov = XmlReadDouble(point, pointEnd, "fov", 90.0);
		value.Selected = XmlHasAttribute(point, pointEnd, "selected");

		std::string qw;
		std::string qx;
		std::string qy;
		std::string qz;
		if (XmlReadAttribute(point, pointEnd, "qw", qw)
			&& XmlReadAttribute(point, pointEnd, "qx", qx)
			&& XmlReadAttribute(point, pointEnd, "qy", qy)
			&& XmlReadAttribute(point, pointEnd, "qz", qz))
		{
			value.R = Quaternion(atof(qw.c_str()), atof(qx.c_str()), atof(qy.c_str()), atof(qz.c_str()));
		}
		else
		{
			const double roll = XmlReadDouble(point, pointEnd, "rx", 0.0);
			const double pitch = XmlReadDouble(point, pointEnd, "ry", 0.0);
			const double yaw = XmlReadDouble(point, pointEnd, "rz", 0.0);
			value.R = Quaternion::FromQREulerAngles(
				QREulerAngles::FromQEulerAngles(QEulerAngles(pitch, yaw, roll)));
		}
		loaded[time] = value;
		point = pointEnd + 1;
	}

	if (loaded.empty())
		return false;

	m_Map.swap(loaded);
	m_PositionInterpMethod = positionInterp;
	m_RotationInterpMethod = rotationInterp;
	m_FovInterpMethod = fovInterp;
	m_Offset = XmlReadDouble(root, rootEnd, "offset", 0.0);
	m_Hold = XmlHasAttribute(root, rootEnd, "hold");

	PositionInterpMethod_set(m_PositionInterpMethod);
	RotationInterpMethod_set(m_RotationInterpMethod);
	FovInterpMethod_set(m_FovInterpMethod);
	DoInterpolationMapChangedAll();
	Changed();
	return true;
}

size_t CamPath::SelectAll()
{
	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		it->second.Selected = true;
	}

	m_SelectedInterp->InterpolationMapChanged();
	Changed();

	return m_Map.size();
}

void CamPath::SelectNone()
{
	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		it->second.Selected = false;
	}

	m_SelectedInterp->InterpolationMapChanged();
	Changed();
}

size_t CamPath::SelectInvert()
{
	size_t selected = 0;

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		it->second.Selected = !it->second.Selected;

		if(it->second.Selected) ++selected;
	}

	m_SelectedInterp->InterpolationMapChanged();
	Changed();

	return selected;
}

size_t CamPath::SelectAdd(size_t min, size_t max)
{
	size_t i = 0;
	size_t selected = 0;

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		it->second.Selected = it->second.Selected || min <= i && i <= max;

		if(it->second.Selected) ++selected;

		++i;
	}

	m_SelectedInterp->InterpolationMapChanged();
	Changed();

	return selected;
}

size_t CamPath::SelectAdd(double min, size_t count)
{
	size_t selected = 0;

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		it->second.Selected = it->second.Selected || min <= it->first && selected < count;

		if(it->second.Selected) ++selected;
	}

	m_SelectedInterp->InterpolationMapChanged();
	Changed();

	return selected;
}

size_t CamPath::SelectAdd(double min, double max)
{
	size_t selected = 0;

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		it->second.Selected = it->second.Selected || min <= it->first && it->first <= max;

		if(it->second.Selected) ++selected;
	}

	m_SelectedInterp->InterpolationMapChanged();
	Changed();

	return selected;
}

void CamPath::SetStart(double t, bool relative)
{
	if(m_Map.size()<1) return;

	CInterpolationMap<CamPathValue> tempMap;

	bool selectAll = true;
	double first = 0;

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		if(it->second.Selected)
		{
			if(selectAll)
			{
				selectAll = false;
				first = it->first;
				break;
			}
		}
	}

	double deltaT = relative ? t : (selectAll ? t -m_Map.begin()->first : t -first);

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		double curT = it->first;
		CamPathValue curValue = it->second;

		if(selectAll || curValue.Selected)
		{
			tempMap[deltaT+curT] = curValue;
		}
		else
		{
			tempMap[curT] = curValue;
		}
	}

	CopyMap(m_Map, tempMap);

	DoInterpolationMapChangedAll();

	Changed();
}
	
void CamPath::SetDuration(double t)
{
	if(m_Map.size()<2) return;

	CInterpolationMap<CamPathValue> tempMap;

	CopyMap(tempMap, m_Map);

	bool selectAll = true;
	double first = 0, last = 0;

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		if(it->second.Selected)
		{
			if(selectAll)
			{
				selectAll = false;
				first = it->first;
				last = first;
			}
			else
			{
				last = it->first;
			}
		}
	}

	double oldDuration = selectAll ? GetDuration() : last -first;

	m_Map.clear();

	double scale = oldDuration ? t / oldDuration : 0.0;
	bool isFirst = true;
	double firstT = 0;

	for(CInterpolationMap<CamPathValue>::const_iterator it = tempMap.begin(); it != tempMap.end(); ++it)
	{
		double curT = it->first;
		CamPathValue curValue = it->second;

		if(selectAll || curValue.Selected)
		{
			if(isFirst)
			{
				m_Map[curT] = curValue;
				firstT = curT;
				isFirst = false;
			}
			else
				m_Map[firstT+scale*(curT-firstT)] = curValue;
		}
		else
			m_Map[curT] = curValue;
	}

	DoInterpolationMapChangedAll();

	Changed();
}

void CamPath::SetPosition(double x, double y, double z, bool setX, bool setY, bool setZ)
{
	if(m_Map.size()<1) return;

	bool selectAll = true;

	for(CInterpolationMap<CamPathValue>::const_iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		if(it->second.Selected)
		{
			if(selectAll)
			{
				selectAll = false;
				break;
			}
		}
	}

	// calcualte mid:

	double minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
	bool first = true;

	for(CInterpolationMap<CamPathValue>::const_iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		if(selectAll || it->second.Selected)
		{
			CamPathValue curValue = it->second;

			if(first)
			{
				minX = curValue.X;
				minY = curValue.Y;
				minZ = curValue.Z;
				maxX = curValue.X;
				maxY = curValue.Y;
				maxZ = curValue.Z;
				first = false;
			}
			else
			{
				minX = std::min(minX, curValue.X);
				minY = std::min(minY, curValue.Y);
				minZ = std::min(minZ, curValue.Z);
				maxX = std::max(maxX, curValue.X);
				maxY = std::max(maxY, curValue.Y);
				maxZ = std::max(maxZ, curValue.Z);
			}
		}
	}

	double x0 = (maxX +minX) / 2;
	double y0 = (maxY +minY) / 2;
	double z0 = (maxZ +minZ) / 2;

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		double curT = it->first;
		CamPathValue curValue = it->second;

		if(selectAll || curValue.Selected)
		{
			if(setX) curValue.X = x +(curValue.X -x0);
			if(setY) curValue.Y = y +(curValue.Y -y0);
			if(setZ) curValue.Z = z +(curValue.Z -z0);

			it->second = curValue;
		}
	}

	m_XInterp->InterpolationMapChanged();
	m_YInterp->InterpolationMapChanged();
	m_ZInterp->InterpolationMapChanged();

	Changed();
}

void CamPath::SetAngles(double yPitch, double zYaw, double xRoll, bool setY, bool setZ, bool setX)
{
	if(m_Map.size()<1) return;

	bool selectAll = true;

	for(CInterpolationMap<CamPathValue>::const_iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		if(it->second.Selected)
		{
			if(selectAll)
			{
				selectAll = false;
				break;
			}
		}
	}

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		double curT = it->first;
		CamPathValue curValue = it->second;

		if(selectAll || curValue.Selected)
		{
			if(setY && setZ && setX) {
				curValue.R = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(yPitch, zYaw, xRoll)));
			} else {
				QEulerAngles angles = curValue.R.ToQREulerAngles().ToQEulerAngles();
				curValue.R = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(
					(setY ? yPitch : angles.Pitch),
					(setZ ? zYaw : angles.Yaw),
					(setX ? xRoll : angles.Roll)
				)));
			}

			it->second = curValue;
		}

	}

	m_RInterp->InterpolationMapChanged();

	Changed();
}

void CamPath::SetFov(double fov)
{
	if(m_Map.size()<1) return;

	bool selectAll = true;

	for(CInterpolationMap<CamPathValue>::const_iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		if(it->second.Selected)
		{
			if(selectAll)
			{
				selectAll = false;
				break;
			}
		}
	}

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		double curT = it->first;
		CamPathValue curValue = it->second;

		if(selectAll || curValue.Selected)
		{
			curValue.Fov = fov;

			it->second = curValue;
		}

	}

	m_FovInterp->InterpolationMapChanged();

	Changed();
}

void CamPath::Rotate(double yPitch, double zYaw, double xRoll)
{
	if(m_Map.size()<1) return;

	bool selectAll = true;

	for(CInterpolationMap<CamPathValue>::const_iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		if(it->second.Selected)
		{
			if(selectAll)
			{
				selectAll = false;
				break;
			}
		}
	}

	// calcualte mid:

	double minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
	bool first = true;

	for(CInterpolationMap<CamPathValue>::const_iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		if(selectAll || it->second.Selected)
		{
			CamPathValue curValue = it->second;

			if(first)
			{
				minX = curValue.X;
				minY = curValue.Y;
				minZ = curValue.Z;
				maxX = curValue.X;
				maxY = curValue.Y;
				maxZ = curValue.Z;
				first = false;
			}
			else
			{
				minX = std::min(minX, curValue.X);
				minY = std::min(minY, curValue.Y);
				minZ = std::min(minZ, curValue.Z);
				maxX = std::max(maxX, curValue.X);
				maxY = std::max(maxY, curValue.Y);
				maxZ = std::max(maxZ, curValue.Z);
			}
		}
	}

	double x0 = (maxX +minX) / 2;
	double y0 = (maxY +minY) / 2;
	double z0 = (maxZ +minZ) / 2;

	// build rotation matrix:
	double R[3][3];
	{
		double angle;
		double sr, sp, sy, cr, cp, cy;

		angle = zYaw * (M_PI*2 / 360);
		sy = sin(angle);
		cy = cos(angle);
		angle = yPitch * (M_PI*2 / 360);
		sp = sin(angle);
		cp = cos(angle);
		angle = xRoll * (M_PI*2 / 360);
		sr = sin(angle);
		cr = cos(angle);

		// R = YAW * (PITCH * ROLL)
		R[0][0] = cy*cp;
		R[0][1] = cy*sp*sr -sy*cr;
		R[0][2] = cy*sp*cr +sy*sr;
		R[1][0] = sy*cp;
		R[1][1] = sy*sp*sr +cy*cr;
		R[1][2] = sy*sp*cr +cy*-sr;
		R[2][0] = -sp;
		R[2][1] = cp*sr;
		R[2][2] = cp*cr;
	}
	Quaternion quatR = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(yPitch, zYaw, xRoll)));

	// rotate:

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		double curT = it->first;
		CamPathValue curValue = it->second;

		if(selectAll || curValue.Selected)
		{
			// update position:
			{
				// translate into origin:
				double x = curValue.X -x0;
				double y = curValue.Y -y0;
				double z = curValue.Z -z0;

				// rotate:
				double Rx = R[0][0]*x +R[0][1]*y +R[0][2]*z;
				double Ry = R[1][0]*x +R[1][1]*y +R[1][2]*z;
				double Rz = R[2][0]*x +R[2][1]*y +R[2][2]*z;

				// translate back:
				curValue.X = Rx +x0;
				curValue.Y = Ry +y0;
				curValue.Z = Rz +z0;
			}

			// update rotation:
			{
				Quaternion quatQ = curValue.R;

				curValue.R = quatR * quatQ;
			}

			// update:
			it->second = curValue;
		}

	}

	m_XInterp->InterpolationMapChanged();
	m_YInterp->InterpolationMapChanged();
	m_ZInterp->InterpolationMapChanged();
	m_RInterp->InterpolationMapChanged();

	Changed();
}

void CamPath::AnchorTransform(double anchorX, double anchorY, double anchorZ, double anchorYPitch, double anchorZYaw, double anchorXRoll, double destX, double destY, double destZ, double destYPitch, double destZYaw, double destXRoll)
{
	if(m_Map.size()<1) return;

	bool selectAll = true;

	for(CInterpolationMap<CamPathValue>::const_iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		if(it->second.Selected)
		{
			if(selectAll)
			{
				selectAll = false;
				break;
			}
		}
	}

	Quaternion quatAnchor = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(anchorYPitch, anchorZYaw, anchorXRoll)));
	Quaternion quatDest = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(destYPitch, destZYaw, destXRoll)));

	// Make sure we take the shortest path:
	double dotProduct = DotProduct(quatDest, quatAnchor);
	if (dotProduct<0.0)
	{
		quatAnchor = -1.0 * quatAnchor;
	}

	Quaternion quatR = quatDest * (/*(1.0 / quatAnchor.Norm()) * */quatAnchor.Conjugate());

	QEulerAngles angles = quatR.ToQREulerAngles().ToQEulerAngles();

	double yPitch = angles.Pitch;
	double zYaw = angles.Yaw;
	double xRoll = angles.Roll;

	// build rotation matrix:
	double R[3][3];
	{
		double angle;
		double sr, sp, sy, cr, cp, cy;

		angle = zYaw * (M_PI*2 / 360);
		sy = sin(angle);
		cy = cos(angle);
		angle = yPitch * (M_PI*2 / 360);
		sp = sin(angle);
		cp = cos(angle);
		angle = xRoll * (M_PI*2 / 360);
		sr = sin(angle);
		cr = cos(angle);

		// R = YAW * (PITCH * ROLL)
		R[0][0] = cy*cp;
		R[0][1] = cy*sp*sr -sy*cr;
		R[0][2] = cy*sp*cr +sy*sr;
		R[1][0] = sy*cp;
		R[1][1] = sy*sp*sr +cy*cr;
		R[1][2] = sy*sp*cr +cy*-sr;
		R[2][0] = -sp;
		R[2][1] = cp*sr;
		R[2][2] = cp*cr;
	}

	// rotate:

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		double curT = it->first;
		CamPathValue curValue = it->second;

		if(selectAll || curValue.Selected)
		{
			// update position:
			{
				// translate into anchor:
				double x = curValue.X -anchorX;
				double y = curValue.Y -anchorY;
				double z = curValue.Z -anchorZ;

				// rotate:
				double Rx = R[0][0]*x +R[0][1]*y +R[0][2]*z;
				double Ry = R[1][0]*x +R[1][1]*y +R[1][2]*z;
				double Rz = R[2][0]*x +R[2][1]*y +R[2][2]*z;

				// translate into destination:
				curValue.X = Rx +destX;
				curValue.Y = Ry +destY;
				curValue.Z = Rz +destZ;
			}

			// update rotation:
			{
				Quaternion quatQ = curValue.R;

				curValue.R = quatR * quatQ;
			}

			// update:
			it->second = curValue;
		}

	}

	m_XInterp->InterpolationMapChanged();
	m_YInterp->InterpolationMapChanged();
	m_ZInterp->InterpolationMapChanged();
	m_RInterp->InterpolationMapChanged();

	Changed();
}

void CamPath::CopyMap(CInterpolationMap<CamPathValue> & dst, CInterpolationMap<CamPathValue> & src)
{
	dst.clear();

	for(CInterpolationMap<CamPathValue>::const_iterator it = src.begin(); it != src.end(); ++it)
	{
		dst[it->first] = it->second;
	}
}

double CamPath::GetDuration() const
{
	if(m_Map.size()<2) return 0.0;

	return (--m_Map.cend())->first - m_Map.cbegin()->first;
}

void CamPath::SetOffset(double value)
{
	m_Offset = value;

	Changed();
}

double CamPath::GetOffset() const
{
	return m_Offset;
}

void CamPath::OnChangedAdd(CamPathChanged pCamPathChanged, void * pUserData) {
	m_OnChanged.emplace_back(pCamPathChanged,pUserData);
}

void CamPath::OnChangedRemove(CamPathChanged pCamPathChanged, void * pUserData) {
	if(m_OnChangedIt != m_OnChanged.end()) {
		auto it = std::find(m_OnChanged.begin(), m_OnChanged.end(), CamPathChangedData(pCamPathChanged,pUserData));
		if(m_OnChangedIt == it)
			m_OnChangedIt = m_OnChanged.erase(it);
		else
			m_OnChanged.erase(it);
	}
}
