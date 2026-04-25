<template>
  <div id="app">
    <div
      v-if="useDesignCanvas"
      class="app-scale-viewport"
      :class="{ 'is-scaled': isScaled }"
      :style="scaleVars"
    >
      <div class="app-scale-shell">
        <div class="app-design-canvas">
          <router-view />
        </div>
      </div>
    </div>
    <router-view v-else />
  </div>
</template>

<script>
export default {
  name: 'App',
  data() {
    return {
      designWidth: 1366,
      designHeight: 768,
      minScale: 0.72,
      appScale: 1
    }
  },
  computed: {
    useDesignCanvas() {
      return this.$route.path !== '/login'
    },
    isScaled() {
      return this.appScale < 1
    },
    scaleVars() {
      return {
        '--app-scale': this.appScale,
        '--design-width': `${this.designWidth}px`,
        '--design-height': `${this.designHeight}px`
      }
    }
  },
  watch: {
    '$route.path': {
      immediate: true,
      handler() {
        this.$nextTick(this.syncScale)
      }
    }
  },
  mounted() {
    this.syncScale()
    window.addEventListener('resize', this.syncScale)
  },
  beforeDestroy() {
    window.removeEventListener('resize', this.syncScale)
  },
  methods: {
    syncScale() {
      const viewportWidth = document.documentElement.clientWidth || window.innerWidth || this.designWidth
      const widthScale = Math.min(viewportWidth / this.designWidth, 1)
      this.appScale = Math.max(this.minScale, widthScale)
    }
  }
}
</script>

<style lang="scss">
#app {
  width: 100%;
  height: 100%;
  font-family: 'Microsoft YaHei', 'PingFang SC', 'Helvetica Neue', Helvetica, Arial, sans-serif;
}

.app-scale-viewport {
  width: 100%;
  height: 100%;
  overflow: auto;
  background: #eef2f6;
}

.app-scale-shell {
  width: 100%;
  height: 100%;
  min-width: var(--design-width);
  min-height: var(--design-height);
}

.app-design-canvas {
  width: 100%;
  height: 100%;
  min-width: var(--design-width);
  min-height: var(--design-height);
  transform-origin: left top;
}

.app-scale-viewport.is-scaled .app-scale-shell {
  width: calc(var(--design-width) * var(--app-scale));
  height: calc(var(--design-height) * var(--app-scale));
  min-width: calc(var(--design-width) * var(--app-scale));
  min-height: calc(var(--design-height) * var(--app-scale));
}

.app-scale-viewport.is-scaled .app-design-canvas {
  width: var(--design-width);
  height: var(--design-height);
  min-width: var(--design-width);
  min-height: var(--design-height);
  transform: scale(var(--app-scale));
}
</style>
