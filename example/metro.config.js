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
    resolveRequest: (context, moduleName, platform) => {
      // Force react to always resolve from example/node_modules so there is
      // only one copy of React in the bundle (two copies break hooks).
      if (moduleName === 'react' || moduleName.startsWith('react/')) {
        return {
          filePath: require.resolve(moduleName, {
            paths: [path.join(__dirname, 'node_modules')],
          }),
          type: 'sourceFile',
        };
      }
      return context.resolveRequest(context, moduleName, platform);
    },
  },
};

module.exports = mergeConfig(defaultConfig, config);
