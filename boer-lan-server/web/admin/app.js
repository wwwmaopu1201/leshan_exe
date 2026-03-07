const { createApp } = Vue;
const { ElMessage, ElMessageBox } = ElementPlus;

// API基础地址
const API_BASE = '/api';

// Axios实例
const request = axios.create({
    baseURL: API_BASE,
    timeout: 10000
});

// 请求拦截器
request.interceptors.request.use(config => {
    const token = localStorage.getItem('token');
    if (token) {
        config.headers.Authorization = `Bearer ${token}`;
    }
    return config;
});

// 响应拦截器
request.interceptors.response.use(
    response => response.data,
    error => {
        if (error.response?.status === 401) {
            localStorage.removeItem('token');
            location.reload();
        }
        ElMessage.error(error.response?.data?.error || '请求失败');
        return Promise.reject(error);
    }
);

const app = createApp({
    data() {
        return {
            isLoggedIn: false,
            currentPage: 'home',
            loginForm: {
                username: '',
                password: ''
            },
            serverInfo: {
                ips: [],
                port: 8088,
                workDir: '',
                dataDir: '',
                os: '',
                arch: ''
            },
            stats: {
                deviceCount: 0,
                onlineDeviceCount: 0,
                userCount: 0,
                operatorCount: 0,
                groupCount: 0
            },
            debugLogs: [],
            groupTree: [],
            users: [],
            operators: [],
            devices: []
        };
    },
    mounted() {
        // 检查是否已登录
        const token = localStorage.getItem('token');
        if (token) {
            this.isLoggedIn = true;
            this.loadData();
        }
    },
    methods: {
        // 登录
        async login() {
            try {
                const res = await request.post('/auth/login', this.loginForm);
                if (res.code === 0) {
                    localStorage.setItem('token', res.data.token);
                    this.isLoggedIn = true;
                    ElMessage.success('登录成功');
                    this.loadData();
                }
            } catch (error) {
                console.error('登录失败', error);
            }
        },

        // 退出登录
        logout() {
            localStorage.removeItem('token');
            this.isLoggedIn = false;
            ElMessage.success('已退出登录');
        },

        // 加载数据
        async loadData() {
            await this.loadServerInfo();
            await this.loadStats();
            await this.loadDebugLogs();
            await this.loadGroupTree();
            await this.loadUsers();
            await this.loadOperators();
            await this.loadDevices();
        },

        // 加载服务器信息
        async loadServerInfo() {
            try {
                const res = await request.get('/system/info');
                if (res.code === 0) {
                    this.serverInfo = res.data;
                }
            } catch (error) {
                console.error('加载服务器信息失败', error);
            }
        },

        // 加载统计信息
        async loadStats() {
            try {
                const res = await request.get('/system/stats');
                if (res.code === 0) {
                    this.stats = res.data;
                }
            } catch (error) {
                console.error('加载统计信息失败', error);
            }
        },

        // 加载调试日志
        async loadDebugLogs() {
            try {
                const res = await request.get('/system/logs', {
                    params: { limit: 50 }
                });
                if (res.code === 0) {
                    this.debugLogs = res.data;
                }
            } catch (error) {
                console.error('加载调试日志失败', error);
            }
        },

        // 清空调试日志
        async clearDebugLogs() {
            try {
                await ElMessageBox.confirm('确定要清空所有调试日志吗?', '警告', {
                    type: 'warning'
                });
                await request.delete('/system/logs');
                this.debugLogs = [];
                ElMessage.success('日志已清空');
            } catch (error) {
                if (error !== 'cancel') {
                    console.error('清空日志失败', error);
                }
            }
        },

        // 加载分组树
        async loadGroupTree() {
            try {
                const res = await request.get('/group/tree');
                if (res.code === 0) {
                    this.groupTree = res.data;
                }
            } catch (error) {
                console.error('加载分组树失败', error);
            }
        },

        // 加载用户列表
        async loadUsers() {
            try {
                const res = await request.get('/user/all');
                if (res.code === 0) {
                    this.users = res.data;
                }
            } catch (error) {
                console.error('加载用户列表失败', error);
            }
        },

        // 加载操作员列表
        async loadOperators() {
            try {
                const res = await request.get('/operator/all');
                if (res.code === 0) {
                    this.operators = res.data;
                }
            } catch (error) {
                console.error('加载操作员列表失败', error);
            }
        },

        // 加载设备列表
        async loadDevices() {
            try {
                const res = await request.get('/device/list');
                if (res.code === 0) {
                    this.devices = res.data;
                }
            } catch (error) {
                console.error('加载设备列表失败', error);
            }
        },

        // 显示网络信息
        async showNetworkInfo() {
            try {
                const res = await request.get('/system/network');
                if (res.code === 0) {
                    const info = res.data.map(item =>
                        `${item.name}: ${item.addresses.join(', ')}`
                    ).join('\n');
                    ElMessageBox.alert(info, '网络信息', {
                        confirmButtonText: '确定'
                    });
                }
            } catch (error) {
                console.error('获取网络信息失败', error);
            }
        },

        // Ping设备
        async pingDevice(ip) {
            try {
                const res = await request.post('/system/ping', null, {
                    params: { ip }
                });
                if (res.code === 0) {
                    ElMessageBox.alert(
                        `<pre>${res.data.output}</pre>`,
                        'Ping结果',
                        {
                            dangerouslyUseHTMLString: true,
                            confirmButtonText: '确定'
                        }
                    );
                }
            } catch (error) {
                console.error('Ping失败', error);
            }
        },

        // 显示Ping对话框
        showPingDialog() {
            ElMessageBox.prompt('请输入要Ping的IP地址', 'Ping设备', {
                confirmButtonText: '确定',
                cancelButtonText: '取消',
                inputPattern: /^(\d{1,3}\.){3}\d{1,3}$/,
                inputErrorMessage: 'IP地址格式不正确'
            }).then(({ value }) => {
                this.pingDevice(value);
            }).catch(() => {});
        },

        // 格式化时间
        formatTime(time) {
            if (!time) return '';
            const date = new Date(time);
            return `${date.getHours()}:${String(date.getMinutes()).padStart(2, '0')}:${String(date.getSeconds()).padStart(2, '0')}`;
        },

        // 获取状态类型
        getStatusType(status) {
            const map = {
                'online': 'success',
                'offline': 'info',
                'working': 'success',
                'idle': 'warning',
                'alarm': 'danger'
            };
            return map[status] || 'info';
        },

        // 新建分组对话框
        showAddGroupDialog() {
            ElMessageBox.prompt('请输入分组名称', '新建分组', {
                confirmButtonText: '确定',
                cancelButtonText: '取消'
            }).then(async ({ value }) => {
                try {
                    await request.post('/group', { name: value });
                    ElMessage.success('创建成功');
                    await this.loadGroupTree();
                } catch (error) {
                    console.error('创建分组失败', error);
                }
            }).catch(() => {});
        },

        // 编辑分组
        async editGroup(group) {
            ElMessageBox.prompt('请输入新的分组名称', '编辑分组', {
                confirmButtonText: '确定',
                cancelButtonText: '取消',
                inputValue: group.name
            }).then(async ({ value }) => {
                try {
                    await request.put(`/group/${group.id}`, { name: value });
                    ElMessage.success('更新成功');
                    await this.loadGroupTree();
                } catch (error) {
                    console.error('更新分组失败', error);
                }
            }).catch(() => {});
        },

        // 删除分组
        async deleteGroup(group) {
            try {
                await ElMessageBox.confirm('确定要删除该分组吗？', '警告', {
                    type: 'warning'
                });
                await request.delete(`/group/${group.id}`);
                ElMessage.success('删除成功');
                await this.loadGroupTree();
            } catch (error) {
                if (error !== 'cancel') {
                    console.error('删除分组失败', error);
                }
            }
        },

        // 新建用户对话框
        showAddUserDialog() {
            ElMessage.info('请使用完整表单功能（开发中）');
        },

        // 编辑用户
        editUser(user) {
            ElMessage.info('编辑功能开发中');
        },

        // 删除用户
        async deleteUser(user) {
            try {
                await ElMessageBox.confirm('确定要删除该用户吗？', '警告', {
                    type: 'warning'
                });
                await request.delete('/user', { data: { ids: [user.ID] } });
                ElMessage.success('删除成功');
                await this.loadUsers();
            } catch (error) {
                if (error !== 'cancel') {
                    console.error('删除用户失败', error);
                }
            }
        },

        // 新建操作员对话框
        showAddOperatorDialog() {
            ElMessage.info('请使用完整表单功能（开发中）');
        },

        // 编辑操作员
        editOperator(operator) {
            ElMessage.info('编辑功能开发中');
        },

        // 删除操作员
        async deleteOperator(operator) {
            try {
                await ElMessageBox.confirm('确定要删除该操作员吗？', '警告', {
                    type: 'warning'
                });
                await request.delete('/operator', { data: { ids: [operator.ID] } });
                ElMessage.success('删除成功');
                await this.loadOperators();
            } catch (error) {
                if (error !== 'cancel') {
                    console.error('删除操作员失败', error);
                }
            }
        }
    }
});

app.use(ElementPlus);
app.mount('#app');
