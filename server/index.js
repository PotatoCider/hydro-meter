const express = require('express')
const Redis = require("ioredis")
const redis = new Redis({ keyPrefix: 'hydro-rpg:' })
const app = express()
const port = 3000

app.use(express.json())

// app.get('/', (req, res) => {
//     res.send('Hello World!')
// })

app.get('/flow_volume', async (req, res) => {
    const { chip_id, user_id } = req.query
    if (chip_id < 0 || chip_id >= 2 ** 32) {
        res.send('Invalid request: chip_id is out of bounds')
        res.status(400)
        return res.end()
    }
    if (user_id < 0 || user_id >= 2 ** 16) {
        res.send('Invalid request: user_id is out of bounds')
        res.status(400)
        return res.end()
    }
    const totalKey = `${chip_id}:${+user_id}:total_flow_volume`
    const dailyKey = `${chip_id}:${+user_id}:daily_flow_volume`
    const weeklyKey = `${chip_id}:${+user_id}:weekly_flow_volume`
    const monthlyKey = `${chip_id}:${+user_id}:monthly_flow_volume`
    const yearlyKey = `${chip_id}:${+user_id}:yearly_flow_volume`

    res.send({
        chip_id, user_id,
        total_flow_volume: +(await redis.get(totalKey)), // +null === 0
        daily_flow_volume: +(await redis.get(dailyKey)),
        weekly_flow_volume: +(await redis.get(weeklyKey)),
        monthly_flow_volume: +(await redis.get(monthlyKey)),
        yearly_flow_volume: +(await redis.get(yearlyKey)),
    })
})

app.post('/flow_volume', async (req, res) => {
    console.log('received POST /flow_volume')
    console.log('body:', req.body)
    const { chip_id, user_id, flow_volume } = req.body
    const totalKey = `${chip_id}:${+user_id}:total_flow_volume`
    const dailyKey = `${chip_id}:${+user_id}:daily_flow_volume`
    const weeklyKey = `${chip_id}:${+user_id}:weekly_flow_volume`
    const monthlyKey = `${chip_id}:${+user_id}:monthly_flow_volume`
    const yearlyKey = `${chip_id}:${+user_id}:yearly_flow_volume`
    const now = new Date()
    const expireDay = new Date()
    const expireWeek = new Date()
    const expireMonth = new Date()
    const expireYear = new Date()
    expireDay.setHours(24, 0, 0, 0) // next midnight

    expireWeek.setDate(now.getDate() + (7 - now.getDay()) % 7) // set to Sunday
    expireWeek.setHours(24, 0, 0, 0)

    expireMonth.setMonth(now.getMonth() + 1, 1)
    expireMonth.setHours(0, 0, 0, 0)
    expireYear.setFullYear(now.getFullYear() + 1, 0, 1)
    expireYear.setHours(0, 0, 0, 0)

    redis.incrbyfloat(totalKey, flow_volume)
    redis.incrbyfloat(dailyKey, flow_volume)
    redis.incrbyfloat(weeklyKey, flow_volume)
    redis.incrbyfloat(monthlyKey, flow_volume)
    redis.incrbyfloat(yearlyKey, flow_volume)

    redis.expireat(dailyKey, Math.floor(expireDay.getTime() / 1000))
    redis.expireat(weeklyKey, Math.floor(expireWeek.getTime() / 1000))
    redis.expireat(monthlyKey, Math.floor(expireMonth.getTime() / 1000))
    redis.expireat(yearlyKey, Math.floor(expireYear.getTime() / 1000))

    res.status(200)
    res.end()
})

app.listen(port, () => {
    console.log(`Listening at http://localhost:${port}`)
})
