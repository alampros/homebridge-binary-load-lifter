import type { API } from 'homebridge'

import { BinaryLoadLifterPlatform } from './platform.js'
import { PLATFORM_NAME, PLUGIN_NAME } from './utils/constants.js'

export default (api: API): void => {
  api.registerPlatform(PLUGIN_NAME, PLATFORM_NAME, BinaryLoadLifterPlatform)
}
