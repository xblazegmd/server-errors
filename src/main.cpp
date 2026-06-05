#include <Geode/Geode.hpp>
#include <Geode/modify/GameLevelManager.hpp>
#include <Geode/modify/CustomSongWidget.hpp>
#include <Geode/modify/InfoLayer.hpp>
#include <Geode/modify/ProfilePage.hpp>

#include <cmath>
#include <string>

#include <xblazegmd.geode-api/include/XblazeAPI.hpp>

using namespace geode::prelude;

/// A special popup that can only appear once at a time
class ErrorPopup final : public FLAlertLayer, public FLAlertLayerProtocol {
public:
	static ErrorPopup* create(const char* title, std::string msg) {
		return ErrorPopup::create(title, std::move(msg), nullptr, nullptr);
	}

	static ErrorPopup* create(const char* title, std::string msg, const char* btn2, geode::Function<void()> btn2Cb, float width = 300.f) {
		if (s_instance) return nullptr;
		auto ret = new ErrorPopup();
		ret->m_btn2Cb = std::move(btn2Cb);
		if (ret->init(ret, title, std::move(msg), "OK", btn2, 350.f, false, 0.f, 1.f)) {
			ret->autorelease();
			s_instance = ret;
			return ret;
		}
		return nullptr;
	}

	/// Safe way to make a popup and show it without risking crashes
	static void createAndShow(const char* title, std::string msg) {
		auto instance = ErrorPopup::create(title, std::move(msg));
		if (instance) instance->show();
	}

	/// Safe way to make a popup and show it without risking crashes
	static void createAndShow(const char* title, std::string msg, const char* btn2, geode::Function<void()> btn2Cb) {
		auto instance = ErrorPopup::create(title, std::move(msg), btn2, std::move(btn2Cb));
		if (instance) instance->show();
	}
private:
	static inline ErrorPopup* s_instance = nullptr;
	geode::Function<void()> m_btn2Cb;

	void FLAlert_Clicked(FLAlertLayer* layer, bool btn2) override {
		if (btn2 && m_btn2Cb) {
			m_btn2Cb();
		}
		s_instance = nullptr;
	}
};

void areTheServersDown() {
	async::spawn(
		xblazeapi::requestGDServers("getGJLevels21.php", fmt::format("type=1&secret={}", xblazeapi::SECRET)),
		[](Result<std::string, int> res) {
			if (res.isErr()) {
				ErrorPopup::createAndShow(
					"Error",
					"The Geometry Dash servers are <cr>down</c> or <co>unreachable</c>"
				);
			}
		}
	);
}

static bool g_ongoingInternetCheck = false;
void internetCheck() {
	if (g_ongoingInternetCheck) return;

	g_ongoingInternetCheck = true;
	async::spawn(
		xblazeapi::doWeHaveInternet(),
		[](bool status) {
			if (!status) {
				xblazeapi::quickErrorNotification("No internet connection!");
			}
			g_ongoingInternetCheck = false;
		}
	);
}

$on_game(Loaded) {
	internetCheck();
}

// Most web requests use GameLevelManager so this is how we'll catch many of the errors
class $modify(GLMHook, GameLevelManager) {
	void onProcessHttpRequestCompleted(extension::CCHttpClient* client, extension::CCHttpResponse* response) {
		GameLevelManager::onProcessHttpRequestCompleted(client, response);

		if (response->getResponseCode() == 500) {
			ErrorPopup::createAndShow(
				"Error",
				"The Geometry Dash servers are <cr>down</c> due to an unexpected <co>internal server error</c>"
			);
			return;
		} else if (response->getResponseCode() == 0) {
			internetCheck();
		}

		// Helpful code from BetterInfo (sry cvolton)
		// (if you need me to remove this I'll figure out what to do)
		auto data = response->getResponseData();
		if ((data->size() > 11 && data->at(0) == 'e')) {
			auto headerVector = response->getResponseHeader();
			auto headers = std::string(headerVector->data(), headerVector->size());

			for (const auto& header : string::split(headers, "\n")) {
				if (header.size() < 14 || !header.starts_with("Retry-After")) continue;

				// rate limited
				auto time = utils::numFromString<int>(header.substr(13)).unwrapOr(0);
				ErrorPopup::createAndShow(
					"Rate Limited",
					fmt::format("You have been <co>rate limited</c> from the Geometry Dash servers.\nTry again in <cy>{}</c>", GameToolbox::getTimeString(time, false))
				);
				return;
			}

			// More helpful code from BetterInfo (sry again cvolton)
			auto dat = std::string(data->data(), data->size());
			if (dat.starts_with("error code:")) {
				auto errCode = utils::numFromString<int>(dat.substr(12)).unwrapOr(0);
				switch (errCode) {
					case 1005:
						ErrorPopup::createAndShow(
							"ISP Ban",
							"Your <cy>Internet Service Provider</c> (ISP) has been <cr>banned</c> from the Geometry Dash servers. If you are using a VPN, please disable it and try again <cl>(error code: 1005)</c>"
						);
						break;
					case 1006:
					case 1007:
					case 1008:
						ErrorPopup::createAndShow(
							"Banned",
							"You have been <cr>IP banned</c> from the Geometry Dash servers. If you are using a VPN, please disable it and try again <cl>(error code: 1006)</c>"
						);
						break;
					case 1015:
						ErrorPopup::createAndShow(
							"Rate Limited",
							"You have been <co>rate limited</c> from the Geometry Dash servers. Please try again later <cl>(error code: 1015)</c>"
						);
						break;
					case 1020:
						ErrorPopup::createAndShow(
							"Maintenance",
							"The Geometry Dash servers are undergoing <co>maintenance</c>. Please try again later <cl>(error code: 1020)</c>"
						);
						break;
					default:
						ErrorPopup::createAndShow(
							"Error",
							fmt::format("An unexpected server error occured <cl>(error code: {})</c>", errCode)
						);
						break;
				}
			}
		}
	}

	// Commenting Issues
	void onUploadCommentCompleted(gd::string response, gd::string tag) {
		if (response == "-10") {
			ErrorPopup::createAndShow(
				"Banned",
				"You have been <co>permanently</c> <cr>banned</c> from commenting. Please contact support if you have any questions"
			);
			return;
		}

		if (string::startsWith(response, "temp")) {
			auto pieces = string::split(response, "_");

			auto duration = utils::numFromString<int>(pieces[1]);
			if (duration) {
				// Perma bans
				auto days = std::round(duration.unwrap() / 86400);
				if (days >= 35) {
					std::string msg = "You have been <cr>banned</c> from making comments for an <cy>indefinite time</c> (most likely <co>permanent</c>).";

					if (pieces.size() > 2) {
						msg += fmt::format("\n\n<cl>Reason: {}</c>", pieces[2]);
					}

					ErrorPopup::createAndShow(
						"Banned",
						msg,
						"Help",
						[] {
							FLAlertLayer::create(
								"Indefinite bans",
								"The in-game timer for comment bans only goes up to <cy>34 days and 22 hours</c>. Bans longer than that are usually much longer, and are likely <co>permanent</c>.\n"
								"You can contact an <cp>Elder Moderator</c> to verify the longevity of the ban\n\n"
								"<cl>P.S. You are seeing this popup thanks to the Server Errors Geode mod. yw /Xblaze</c>",
								"OK"
							)->show();
						}
					);
					return;
				}

				// Insecure password
				if (pieces.size() > 2 && string::startsWith(pieces[2], "You cant comment because your password is insecure")) {
					ErrorPopup::createAndShow(
						"Unsafe Password",
						"Your password is <co>too insecure</c>. Please change your password to reenable commenting"
					);
					return;
				}
			} else {
				log::error("Could not convert ban duration to int: {}", duration.unwrapErr());
			}

			// There is no reason to modify the existing comment ban
			// popup so we'll leave it as it is for now
		}

		GameLevelManager::onUploadCommentCompleted(response, tag);
	}
};

class $modify(CSWHook, CustomSongWidget) {
	void loadSongInfoFailed(int id, GJSongError errorType) {
		CustomSongWidget::loadSongInfoFailed(id, errorType);

		if (errorType == GJSongError::FailedToFetch) {
			async::spawn(
				web::WebRequest()
					.get("http://newgrounds.com/"),
				[](web::WebResponse res) {
					if (!res.ok()) {
						ErrorPopup::createAndShow(
							"Error",
							fmt::format("The Newgrounds servers are likely <cr>down</c>. Please try again later.\n<cl>(status code: {})</c>", res.code())
						);
					}
				}
			);
		}
	}
};

class $modify(ILHook, InfoLayer) {
	void loadCommentsFailed(const char* key) {
		InfoLayer::loadCommentsFailed(key);
		async::spawn(
			xblazeapi::requestGDServers("getGJLevels21.php", fmt::format("type=1&secret={}", xblazeapi::SECRET)),
			[](Result<std::string, int> res) {
				if (res.isErr()) {
					ErrorPopup::createAndShow(
						"Error",
						"The Geometry Dash servers are <cr>down</c> or <co>unreachable</c>"
					);
				}
			}
		);
	}

	void commentUploadFailed(int parentID, CommentError errorType) {
		InfoLayer::commentUploadFailed(parentID, errorType);
		areTheServersDown();
	}
};

class $modify(PPHook, ProfilePage) {
	void commentUploadFailed(int parentID, CommentError errorType) {
		ProfilePage::commentUploadFailed(parentID, errorType);
		areTheServersDown();
	}
};