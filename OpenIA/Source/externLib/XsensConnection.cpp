//  Copyright (c) 2003-2019 Xsens Technologies B.V. or subsidiaries worldwide.
//  All rights reserved.
//  
//  Redistribution and use in source and binary forms, with or without modification,
//  are permitted provided that the following conditions are met:
//  
//  1.	Redistributions of source code must retain the above copyright notice,
//  	this list of conditions, and the following disclaimer.
//  
//  2.	Redistributions in binary form must reproduce the above copyright notice,
//  	this list of conditions, and the following disclaimer in the documentation
//  	and/or other materials provided with the distribution.
//  
//  3.	Neither the names of the copyright holders nor the names of their contributors
//  	may be used to endorse or promote products derived from this software without
//  	specific prior written permission.
//  
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
//  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
//  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
//  THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
//  SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
//  OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
//  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY OR
//  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.THE LAWS OF THE NETHERLANDS 
//  SHALL BE EXCLUSIVELY APPLICABLE AND ANY DISPUTES SHALL BE FINALLY SETTLED UNDER THE RULES 
//  OF ARBITRATION OF THE INTERNATIONAL CHAMBER OF COMMERCE IN THE HAGUE BY ONE OR MORE 
//  ARBITRATORS APPOINTED IN ACCORDANCE WITH SAID RULES.
//  

#include "XsensConnection.h"
#include <thread>
//Xsens part-----------------------------------
#define BUF_SIZE 255
#define FOCAL_LENGTH -8.0

//Xsens
float mtRoll = 0; float mtPitch = 0; float mtYaw = 0;
float Qut_x = 0; float Qut_y = 0; float Qut_z = 0; float Qut_w = 0;

//XSens handle
XsDevicePtr wirelessMasterDevice;
XsDevicePtr mtwDevice;

//Quaternion Object
quaternion Quat;
quaternion InvQuat;

bool calibrate = true;  // Reset Quaternion
						//----------------------------------------------

						//Xsens Function------------------------------------
						//----------------------------------------------------------------------
						// Callback handler for wireless master
						//----------------------------------------------------------------------
class WirelessMasterCallback : public XsCallback
{
public:
	typedef std::set<XsDevice*> XsDeviceSet;

	XsDeviceSet getWirelessMTWs() const
	{
		XsMutexLocker lock(m_mutex);
		return m_connectedMTWs;
	}

protected:
	virtual void onConnectivityChanged(XsDevice* dev, XsConnectivityState newState)
	{
		XsMutexLocker lock(m_mutex);
		switch (newState)
		{
		case XCS_Disconnected:		/*!< Device has disconnected, only limited informational functionality is available. */
									//std::cout << "\nEVENT: MTW Disconnected -> " << *dev << std::endl;
			m_connectedMTWs.erase(dev);
			break;
		case XCS_Rejected:			/*!< Device has been rejected and is disconnected, only limited informational functionality is available. */
									//std::cout << "\nEVENT: MTW Rejected -> " << *dev << std::endl;
			m_connectedMTWs.erase(dev);
			break;
		case XCS_PluggedIn:			/*!< Device is connected through a cable. */
									//std::cout << "\nEVENT: MTW PluggedIn -> " << *dev << std::endl;
			m_connectedMTWs.erase(dev);
			break;
		case XCS_Wireless:			/*!< Device is connected wirelessly. */
									//std::cout << "\nEVENT: MTW Connected -> " << *dev << std::endl;
			m_connectedMTWs.insert(dev);
			break;
		case XCS_File:				/*!< Device is reading from a file. */
									//std::cout << "\nEVENT: MTW File -> " << *dev << std::endl;
			m_connectedMTWs.erase(dev);
			break;
		case XCS_Unknown:			/*!< Device is in an unknown state. */
									//std::cout << "\nEVENT: MTW Unknown -> " << *dev << std::endl;
			m_connectedMTWs.erase(dev);
			break;
		default:
			//std::cout << "\nEVENT: MTW Error -> " << *dev << std::endl;
			m_connectedMTWs.erase(dev);
			break;
		}
	}
private:
	mutable XsMutex m_mutex;
	XsDeviceSet m_connectedMTWs;
};

//----------------------------------------------------------------------
// Callback handler for MTw
// Handles onDataAvailable callbacks for MTW devices
//----------------------------------------------------------------------
class MtwCallback : public XsCallback
{
public:
	MtwCallback(int mtwIndex, XsDevice* device)
		:m_mtwIndex(mtwIndex)
		, m_device(device)
	{}

	bool dataAvailable() const
	{
		XsMutexLocker lock(m_mutex);
		return !m_packetBuffer.empty();
	}

	XsDataPacket const * getOldestPacket() const
	{
		XsMutexLocker lock(m_mutex);
		XsDataPacket const * packet = &m_packetBuffer.front();
		return packet;
	}

	void deleteOldestPacket()
	{
		XsMutexLocker lock(m_mutex);
		m_packetBuffer.pop_front();
	}

	int getMtwIndex() const
	{
		return m_mtwIndex;
	}

	XsDevice const & device() const
	{
		assert(m_device != 0);
		return *m_device;
	}

	

protected:
	virtual void onLiveDataAvailable(XsDevice*, const XsDataPacket* packet)
	{
		XsMutexLocker lock(m_mutex);
		// NOTE: Processing of packets should not be done in this thread.

		m_packetBuffer.push_back(*packet);
		if (m_packetBuffer.size() > 300)
		{
			std::cout << std::endl;
			deleteOldestPacket();
		}
	}

private:
	mutable XsMutex m_mutex;
	std::list<XsDataPacket> m_packetBuffer;
	int m_mtwIndex;
	XsDevice* m_device;
};

/*! \brief Stream insertion operator overload for XsPortInfo */
std::ostream& operator << (std::ostream& out, XsPortInfo const & p)
{
	out << "Port: " << std::setw(2) << std::right << p.portNumber() << " (" << p.portName().toStdString() << ") @ "
		<< std::setw(7) << p.baudrate() << " Bd"
		<< ", " << "ID: " << p.deviceId().toString().toStdString()
		;
	return out;
}

/*! \brief Stream insertion operator overload for XsDevice */
std::ostream& operator << (std::ostream& out, XsDevice const & d)
{
	out << "ID: " << d.deviceId().toString().toStdString() << " (" << d.productCode().toStdString() << ")";
	return out;
}

/*! \brief Given a list of update rates and a desired update rate, returns the closest update rate to the desired one */
int findClosestUpdateRate(const XsIntArray& supportedUpdateRates, const int desiredUpdateRate)
{
	if (supportedUpdateRates.empty())
	{
		return 0;
	}

	if (supportedUpdateRates.size() == 1)
	{
		return supportedUpdateRates[0];
	}

	int uRateDist = -1;
	int closestUpdateRate = -1;
	for (XsIntArray::const_iterator itUpRate = supportedUpdateRates.begin(); itUpRate != supportedUpdateRates.end(); ++itUpRate)
	{
		const int currDist = std::abs(*itUpRate - desiredUpdateRate);

		if ((uRateDist == -1) || (currDist < uRateDist))
		{
			uRateDist = currDist;
			closestUpdateRate = *itUpRate;
		}
	}
	return closestUpdateRate;
}

std::vector<MtwCallback*> mtwCallbacks; // Callbacks for mtw devices


bool XsensConnection::xmtConnect()
{
	const int desiredUpdateRate = 60;	// Use 120 Hz update rate for 1-5 MTWs
	const int desiredRadioChannel = 19;	// Use radio channel 19 for wireless master. Available [11...25] and -1 for disable

	WirelessMasterCallback wirelessMasterCallback; // Callback for wireless master
	std::vector<MtwCallback*> mtwCallbacks; // Callbacks for mtw devices

											//std::cout << "Constructing XsControl..." << std::endl;
	XsControl* control = XsControl::construct();
	if (control == 0)
	{
		std::cout << "Failed to construct XsControl instance." << std::endl;
	}

	try
	{
		//std::cout << "Scanning ports..." << std::endl;
		XsPortInfoArray detectedDevices = XsScanner::scanPorts();


		XsPortInfoArray::const_iterator wirelessMasterPort = detectedDevices.begin();
		while (wirelessMasterPort != detectedDevices.end() && !wirelessMasterPort->deviceId().isWirelessMaster())
		{
			++wirelessMasterPort;
		}
		if (wirelessMasterPort == detectedDevices.end())
		{
			throw std::runtime_error("No wireless masters found");
		}


		//std::cout << "Opening port..." << std::endl;
		if (!control->openPort(wirelessMasterPort->portName().toStdString(), wirelessMasterPort->baudrate()))
		{
			std::ostringstream error;
			error << "Failed to open port " << *wirelessMasterPort;
			throw std::runtime_error(error.str());
		}

		wirelessMasterDevice = control->device(wirelessMasterPort->deviceId());
		if (wirelessMasterDevice == 0)
		{
			std::ostringstream error;
			error << "Failed to construct XsDevice instance: " << *wirelessMasterPort;
			throw std::runtime_error(error.str());
		}

		std::cout << "XsDevice connected @ " << *wirelessMasterDevice << std::endl;

		if (!wirelessMasterDevice->gotoConfig())
		{
			std::ostringstream error;

			throw std::runtime_error(error.str());
		}

		
		wirelessMasterDevice->addCallbackHandler(&wirelessMasterCallback);

		if (!wirelessMasterDevice->setUpdateRate(desiredUpdateRate))
		{
			std::ostringstream error;
			error << "Failed to set update rate: " << *wirelessMasterDevice;
			throw std::runtime_error(error.str());
		}

		if (wirelessMasterDevice->isRadioEnabled())
		{
			if (!wirelessMasterDevice->disableRadio())
			{
				std::ostringstream error;
				error << "Failed to disable radio channel: " << *wirelessMasterDevice;
				throw std::runtime_error(error.str());
			}
		}

		if (!wirelessMasterDevice->enableRadio(desiredRadioChannel))
		{
			std::ostringstream error;
			//error << "Failed to set radio channel: " << *wirelessMasterDevice;
			throw std::runtime_error(error.str());
		}

		//std::cout << "Waiting for MTW to wirelessly connect...\n" << std::endl;

		size_t connectedMTWCount = wirelessMasterCallback.getWirelessMTWs().size();
		bool count_Complited = false;
		do
		{
			XsTime::msleep(100);

			while (true)
			{
				size_t nextCount = wirelessMasterCallback.getWirelessMTWs().size();
				if (nextCount != connectedMTWCount)
				{
					std::cout << "Number of connected MTWs: " << nextCount << std::endl;
					connectedMTWCount = nextCount;
					devicescount = nextCount;
					isRunning = true;
					count_Complited = true;
				}
				else
				{
					break;
				}
			}

			if (count_Complited) {
				XsDeviceIdArray allDeviceIds_print = control->deviceIds();
				for (XsDeviceIdArray::const_iterator i = allDeviceIds_print.begin(); i != allDeviceIds_print.end(); ++i)
				{
					if (i->isMtw())
					{
						/*XsDevicePtr mtwDevice = control->device(*i);
						std::cout<<"reset:" <<mtwDevice->resetOrientation(XRM_Alignment) << std::endl;*/

						std::cout << "Device: " << *i << " connected." << std::endl;
					}
				}
				count_Complited = false;
			}

		} while (waitForConnections);


		//std::cout << "Starting measurement..." << std::endl;
		if (!wirelessMasterDevice->gotoMeasurement())
		{
			std::ostringstream error;
			error << "Failed to goto measurement mode: " << *wirelessMasterDevice;
			throw std::runtime_error(error.str());
		}

		//std::cout << "Getting XsDevice instances for all MTWs..." << std::endl;
		XsDeviceIdArray allDeviceIds = control->deviceIds();
		XsDeviceIdArray mtwDeviceIds;
		
		for (XsDeviceIdArray::const_iterator i = allDeviceIds.begin(); i != allDeviceIds.end(); ++i)
		{
			if (i->isMtw())
			{
				
				mtwDeviceIds.push_back(*i);
				//std::cout << "Device: " << *i << " Connected." << std::endl;
			}
		}
		
		for (XsDeviceIdArray::const_iterator i = mtwDeviceIds.begin(); i != mtwDeviceIds.end(); ++i)
		{
			XsDevicePtr mtwDevice = control->device(*i);
			if (mtwDevice != 0)
			{			
				
				std::string mtw_id = mtwDevice->deviceId().toString().toStdString();
								
				if (mtw_id == "00B4391F" || mtw_id == "00B43808" || mtw_id == "00B438C7" || mtw_id == "00B438AE" || mtw_id == "00B43923" || mtw_id == "00B43926" ||
					mtw_id == "00B427A3" || mtw_id == "00B42780" || mtw_id == "00B4278B" || mtw_id == "00B42790" || mtw_id == "00B42799" || mtw_id == "00B4279F")//00B438AE & 00B43926
				{
					mtwDevices.push_back(mtwDevice);
					XsFilterProfileArray filters;
					
					filters = mtwDevice->availableOnboardFilterProfiles();

					for (XsFilterProfileArray::const_iterator i = filters.begin(); i != filters.end(); ++i)
					{
						std::cout << "Filter Profile: " << filters.indexOf(i)<< std::endl;
						
					}
					
					//std::cout << "\n reset->" << mtw_id <<":"<< mtwDevice->resetOrientation(XRM_Alignment) << std::endl;
					
				}
								
			}
			else
			{
				throw std::runtime_error("Failed to create an MTW XsDevice instance");
			}
		}

		mtwCallbacks.resize(mtwDevices.size());
		for (int i = 0; i < (int)mtwDevices.size(); ++i)
		{
			
			mtwCallbacks[i] = new MtwCallback(i, mtwDevices[i]);
			mtwDevices[i]->addCallbackHandler(mtwCallbacks[i]);
			//mtwDevices[i]->resetOrientation(XRM_Alignment);
		}

		unsigned int printCounter = 0;
		std::vector<XsQuaternion> quaterdata(mtwCallbacks.size());
		//std::vector<XsVector3> posdata(mtwCallbacks.size());
		std::cout << "Xsens ready! Stand in attention pose for calibration- Press 'v'" << std::endl;

		int mtw_count = 0;
		bool onetime = true;
		
		
		

		while (isRunning)
		{
			XsTime::msleep(0);

			/*time_t curr_time;
			curr_time = time(NULL);
			tm *tm_local = localtime(&curr_time);
			std::cout <<"Time:"<< tm_local->tm_hour <<"\t" << tm_local->tm_min << "\t" << tm_local->tm_sec << std::endl;*/

			for (size_t i = 0; i < mtwCallbacks.size(); ++i)
			{
				if (mtwCallbacks[i]->dataAvailable())
				{
					newDataAvailable = true;
					XsDataPacket const * packet = mtwCallbacks[i]->getOldestPacket();
					//packet->containsPositionLLA = true;
					quaterdata[i] = packet->orientationQuaternion();
					//posdata[i] = packet->positionLLA();
					//packet->positionLLA();
					
					//packet->accelerationHR
					//packet->containsVelocity   calibratedAcceleration
					mtwCallbacks[i]->deleteOldestPacket();					
				}
			}


			if (newDataAvailable)
			{
				
				for (size_t id = 0; id < mtwCallbacks.size(); ++id)
				{
					Quat.mData[0] = quaterdata[id].x();
					Quat.mData[1] = quaterdata[id].y();
					Quat.mData[2] = quaterdata[id].z();
					Quat.mData[3] = quaterdata[id].w();
					
										
					if (id == 0)
					{ 
						xsIMU.b0 = Quat;
						//xsIMU.pos = posdata[0];
					}
				
					if (id == 1)
					{
						xsIMU.b1 = Quat;						
					}

					if (id == 2) 
					{
						xsIMU.b2 = Quat;
					}

					if (id == 3) 
					{
						xsIMU.b3 = Quat;
					}

					if (id == 4) 
					{
						xsIMU.b4 = Quat;
					}
					if (id == 5) 
					{
						xsIMU.b5 = Quat;
					}
					
					if (id == 6)
					{
						xsIMU.b6 = Quat;
					}

					if (id == 7)
					{
						xsIMU.b7 = Quat;
					}

					if (id == 8) 
					{
						xsIMU.b8 = Quat;
					}

					if (id == 9) 
					{
						xsIMU.b9 = Quat;
					}
										
					if (bxMTdisconnect)
					{
						std::cout << "Closing XsControl..." << std::endl;
						control->close();

						for (std::vector<MtwCallback*>::iterator i = mtwCallbacks.begin(); i != mtwCallbacks.end(); ++i)
						{
							delete (*i);
						}
						std::cout << "Successful exit." << std::endl;
						closeMtW_Succes = true;
						return true;
					}

				}				
			}
			//std::this_thread::sleep_for(std::chrono::milliseconds(10000));
			//Sleep(100);
		}

		return true;
	}
	catch (std::exception const & ex)
	{
		std::cout << ex.what() << std::endl;
	}
	catch (...)
	{
		std::cout << "An unknown fatal error has occured. Aborting." << std::endl;
	}
	std::cout << "Closing XsControl..." << std::endl;
	control->close();

	for (std::vector<MtwCallback*>::iterator i = mtwCallbacks.begin(); i != mtwCallbacks.end(); ++i)
	{
		delete (*i);
	}
}