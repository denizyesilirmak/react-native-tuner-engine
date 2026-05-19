const path = require('path');
const { getDefaultConfig, mergeConfig } = require('@react-native/metro-config');

const root = path.resolve(__dirname, '..');

const defaultConfig = getDefaultConfig(__dirname);

/**
 * Metro configuration for monorepo setup.
 * Replaces react-native-monorepo-config which is incompatible with RN 0.85
 * (blockList is now a RegExp, not an array).
 *
 * https://facebook.github.io/metro/docs/configuration
 *
 * @type {import('metro-config').MetroConfig}
 */
const config = {
  watchFolders: [root],

  resolver: {
    nodeModulesDirs: [
      path.resolve(__dirname, 'node_modules'),
      path.resolve(root, 'node_modules'),
    ],
    extraNodeModules: {
      'react-native-tuner-engine': root,
    },
    resolverMainFields: ['react-native', 'source', 'browser', 'main'],
  },
};

module.exports = mergeConfig(defaultConfig, config);
