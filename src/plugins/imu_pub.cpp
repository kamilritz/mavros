/**
 * @brief IMU publish plugin
 * @file imu_pub.cpp
 * @author Vladimit Ermkov <voon341@gmail.com>
 */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <mavros/mavros_plugin.h>
#include <pluginlib/class_list_macros.h>
#include <tf/transform_datatypes.h>

#include <sensor_msgs/Imu.h>
#include <sensor_msgs/MagneticField.h>
#include <sensor_msgs/Temperature.h>

namespace mavplugin {

class IMUPubPlugin : public MavRosPlugin {
public:
	IMUPubPlugin()
	{
		imu_raw = {
			0,
			0.0, 0.0, 0.0, // accel
			0.0, 0.0, 0.0, // gyro
			0.0, 0.0, 0.0, // magn
			0.0, 0.0, 0.0, // pressure
			0.0,           // temp
			0
		};
	};

	void initialize(ros::NodeHandle &nh,
			const boost::shared_ptr<mavconn::MAVConnInterface> &mav_link,
			diagnostic_updater::Updater &diag_updater)
	{
		nh.param<std::string>("imu/frame_id", frame_id, "fcu");

		imu_pub = nh.advertise<sensor_msgs::Imu>("imu", 10);
		magn_pub = nh.advertise<sensor_msgs::MagneticField>("mag", 10);
		temp_pub = nh.advertise<sensor_msgs::Imu>("temperature", 10);
		imu_raw_pub = nh.advertise<sensor_msgs::Imu>("raw/imu", 10);
	}

	std::string get_name() {
		return "IMUPub";
	}

	std::vector<uint8_t> get_supported_messages() {
		return {
			MAVLINK_MSG_ID_ATTITUDE,
			MAVLINK_MSG_ID_HIGHRES_IMU
		};
	}

	void message_rx_cb(const mavlink_message_t *msg, uint8_t sysid, uint8_t compid) {
		switch (msg->msgid) {
		case MAVLINK_MSG_ID_ATTITUDE:
			if (imu_pub.getNumSubscribers() > 0) {
				mavlink_attitude_t att;
				mavlink_msg_attitude_decode(msg, &att);

				sensor_msgs::ImuPtr imu_msg(new sensor_msgs::Imu);

				// TODO: check/verify that these are body-fixed
				imu_msg->orientation = tf::createQuaternionMsgFromRollPitchYaw(
						att.roll, -att.pitch, -att.yaw);

				imu_msg->angular_velocity.x = att.rollspeed;
				imu_msg->angular_velocity.y = -att.pitchspeed;
				imu_msg->angular_velocity.z = -att.yawspeed;

				imu_msg->linear_acceleration.x = imu_raw.xacc;
				imu_msg->linear_acceleration.y = -imu_raw.yacc;
				imu_msg->linear_acceleration.z = -imu_raw.zacc;

				// TODO: can we fill in the covariance here
				// from a parameter that we set from the specs/experience?
				std::fill(imu_msg->orientation_covariance.begin(),
						imu_msg->orientation_covariance.end(), 0);
				std::fill(imu_msg->angular_velocity_covariance.begin(),
						imu_msg->angular_velocity_covariance.end(), 0);
				std::fill(imu_msg->linear_acceleration_covariance.begin(),
						imu_msg->linear_acceleration_covariance.end(), 0);

				imu_msg->header.frame_id = frame_id;
				imu_msg->header.seq = imu_raw.time_usec / 1000;
				imu_msg->header.stamp = ros::Time::now();

				imu_pub.publish(imu_msg);
			}
			break;

		case MAVLINK_MSG_ID_HIGHRES_IMU:
			{
				mavlink_msg_highres_imu_decode(msg, &imu_raw);

				std_msgs::Header header;
				header.stamp = ros::Time::now();
				header.seq = imu_raw.time_usec / 1000;
				header.frame_id = frame_id;

				if (imu_raw_pub.getNumSubscribers() > 0 &&
						imu_raw.fields_updated & 0x003f) {
					sensor_msgs::ImuPtr imu_msg(new sensor_msgs::Imu);
					// TODO: same as for ATTITUDE
					imu_msg->angular_velocity.x = imu_raw.xgyro;
					imu_msg->angular_velocity.y = -imu_raw.ygyro;
					imu_msg->angular_velocity.z = -imu_raw.xgyro;

					imu_msg->linear_acceleration.x = imu_raw.xacc;
					imu_msg->linear_acceleration.y = -imu_raw.yacc;
					imu_msg->linear_acceleration.z = -imu_raw.zacc;

					imu_msg->orientation_covariance[0] = -1;
					std::fill(imu_msg->angular_velocity_covariance.begin(),
							imu_msg->angular_velocity_covariance.end(), 0);
					std::fill(imu_msg->linear_acceleration_covariance.begin(),
							imu_msg->linear_acceleration_covariance.end(), 0);

					imu_msg->header = header;
					imu_raw_pub.publish(imu_msg);
				}
				if (magn_pub.getNumSubscribers() > 0 &&
						imu_raw.fields_updated & 0x01c0) {
					const double gauss_to_tesla = 1.0e-4;
					sensor_msgs::MagneticFieldPtr magn_msg(new sensor_msgs::MagneticField);

					magn_msg->magnetic_field.x = imu_raw.xmag * gauss_to_tesla;
					magn_msg->magnetic_field.y = imu_raw.ymag * gauss_to_tesla;
					magn_msg->magnetic_field.z = imu_raw.zmag * gauss_to_tesla;

					// TODO: again covariance
					std::fill(magn_msg->magnetic_field_covariance.begin(),
							magn_msg->magnetic_field_covariance.end(), 0);

					magn_msg->header = header;
					magn_pub.publish(magn_msg);
				}
				if (imu_raw.fields_updated & 0x0e00) {
					/* TODO: pressure & alt */
				}
				if (temp_pub.getNumSubscribers() > 0 &&
						imu_raw.fields_updated & 0x1000) {
					sensor_msgs::TemperaturePtr temp_msg(new sensor_msgs::Temperature);

					temp_msg->temperature = imu_raw.temperature;
					temp_msg->header = header;
					temp_pub.publish(temp_msg);
				}
			}
			break;
		};
	}

private:
	std::string frame_id;

	ros::Publisher imu_pub;
	ros::Publisher imu_raw_pub;
	ros::Publisher magn_pub;
	ros::Publisher temp_pub;

	mavlink_highres_imu_t imu_raw;
};

}; // namespace mavplugin

PLUGINLIB_EXPORT_CLASS(mavplugin::IMUPubPlugin, mavplugin::MavRosPlugin)
