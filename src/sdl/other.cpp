// Copyright 2021 Jan Niklas Hasse <jhasse@bixense.com>
// For conditions of distribution and use, see copyright notice in LICENSE.txt

#include "../jngl/other.hpp"

#if defined(__has_include) && __has_include(<SDL_locale.h>)
#include <SDL_locale.h>
#endif

namespace jngl {

std::string getPreferredLanguage() {
#if defined(__has_include) && __has_include(<SDL_locale.h>)
	SDL_Locale* locale = SDL_GetPreferredLocales();
	if (locale && locale->language && locale->language[0] != '\0' && locale->language[1] != '\0' &&
	    locale->language[2] == '\0') {
		return locale->language;
	}
#endif
	return "en";
}

} // namespace jngl
