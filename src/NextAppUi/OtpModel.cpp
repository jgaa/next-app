#include "OtpModel.h"
#include "ServerComm.h"

OtpModel::OtpModel(QObject *parent)
: QObject(parent)
{
}

void OtpModel::requestOtpForNewDevice()
{
    ServerComm::instance().requestOtp([this](auto val) {
        if (std::holds_alternative<ServerComm::CbError>(val)) {
            const auto why = std::get<ServerComm::CbError>(val).message;
            LOG_WARN_N << "Failed to get categories: " << why;
            error_ = tr("Failed to get OTP: %1").arg(why);
            emit errorChanged();
            return;
        }

        const auto& data = std::get<nextapp::pb::Status>(val);

        if (data.hasOtpResponse()) {
            otp_ = data.otpResponse().otp();
            email_ = data.otpResponse().email();
            emit otpChanged();
            emit emailChanged();

            error_.clear();
            emit errorChanged();
        } else {
            error_ = tr("Missing OTP in reply from server");
            emit errorChanged();
        }
    });
}
