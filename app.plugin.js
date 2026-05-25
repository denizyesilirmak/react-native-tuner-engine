const {
  withInfoPlist,
  withAndroidManifest,
  createRunOncePlugin,
} = require('@expo/config-plugins');

const pkg = require('./package.json');

const MICROPHONE_USAGE =
  'This app needs access to the microphone to detect pitch and tune your instrument.';

function withTunerEngineMicrophoneIOS(config, { microphonePermission } = {}) {
  return withInfoPlist(config, (config) => {
    config.modResults.NSMicrophoneUsageDescription =
      microphonePermission || MICROPHONE_USAGE;
    return config;
  });
}

function withTunerEngineMicrophoneAndroid(config) {
  return withAndroidManifest(config, (config) => {
    const manifest = config.modResults.manifest;

    if (!manifest['uses-permission']) {
      manifest['uses-permission'] = [];
    }

    const hasPermission = manifest['uses-permission'].some(
      (perm) =>
        perm.$?.['android:name'] === 'android.permission.RECORD_AUDIO'
    );

    if (!hasPermission) {
      manifest['uses-permission'].push({
        $: { 'android:name': 'android.permission.RECORD_AUDIO' },
      });
    }

    return config;
  });
}

function withTunerEngine(config, props = {}) {
  config = withTunerEngineMicrophoneIOS(config, props);
  config = withTunerEngineMicrophoneAndroid(config);
  return config;
}

module.exports = createRunOncePlugin(withTunerEngine, pkg.name, pkg.version);
